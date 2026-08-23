#include <QApplication>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QPainter>
#include <QVector3D>
#include <QVBoxLayout>
#include <QLabel>
#include <QSet>
#include <QTime>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QElapsedTimer>
#include <vector>
#include <cmath>
#include <QDebug>
#include <OpenGL/glu.h>
#include <GLUT/glut.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Face indices
enum {
    FACE_X_NEG = 0, // left   (-X)
    FACE_X_POS = 1, // right  (+X)
    FACE_Y_NEG = 2, // back   (-Y)
    FACE_Y_POS = 3, // front  (+Y)
    FACE_Z_NEG = 4, // bottom (-Z)
    FACE_Z_POS = 5  // top    (+Z)
};

struct FaceData {
    int texId = 0;
    bool interactive = false;
    int interactionType = 0; // 0=none, 1=url/movie, 2=teleport
    QString url;
};

struct Voxel {
    bool active = false;
    FaceData faces[6];

    int age = 0;
    int type = 0;
    float weight = 1.0f;
    float affectorValue = 0.0f;

    int getAge() const { return age; }
    int getType() const { return type; }
    float getWeight() const { return weight; }
    float getAffector() const { return affectorValue; }
};

struct Entity {
    QVector3D pos;
    QVector3D vel;
};

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    OpenGLWidget(QWidget *parent = nullptr)
        : QOpenGLWidget(parent), gridSize(20), distance(25.0f),
          yaw(45.0f), pitch(35.0f), fpYaw(-90.0f), fpPitch(0.0f),
          currentTextureIdx(0), firstPerson(false) {

        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true); // For first-person mouse look

        voxelGrid.resize(gridSize * gridSize * gridSize);
        target = QVector3D(0.0f, 0.0f, 5.0f);

        player.pos = QVector3D(0.0f, 0.0f, 2.0f);
        player.vel = QVector3D(0.0f, 0.0f, 0.0f);
        lastTime = QTime::currentTime();
        lastInteractionTime = 0;

        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            updatePhysics();
            update();
        });
        timer->start(16);
    }

    void setInfoLabel(QLabel* lbl) { infoLabel = lbl; }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.4f, 0.6f, 0.9f, 1.0f);

        textures.push_back(createTexture(QColor(139, 69, 19), "Dirt"));
        textures.push_back(createTexture(QColor(128, 128, 128), "Stone"));
        textures.push_back(createTexture(QColor(34, 139, 34), "Grass"));
        textures.push_back(createTexture(QColor(178, 34, 34), "Brick"));
        textures.push_back(createTexture(QColor(240, 220, 150), "Sand"));
        textures.push_back(createTexture(QColor(50, 50, 200), "Poster")); // Special poster texture
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(60.0, static_cast<double>(w) / h, 0.1, 100.0);
        glMatrixMode(GL_MODELVIEW);
    }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();

        if (firstPerson) {
            // First-person camera at player eye position
            float eyeHeight = 1.2f;
            float ex = player.pos.x();
            float ey = player.pos.y();
            float ez = player.pos.z() + eyeHeight;

            float lookX = ex + cos(fpPitch * M_PI / 180.0f) * cos(fpYaw * M_PI / 180.0f);
            float lookY = ey + cos(fpPitch * M_PI / 180.0f) * sin(fpYaw * M_PI / 180.0f);
            float lookZ = ez + sin(fpPitch * M_PI / 180.0f);

            gluLookAt(ex, ey, ez, lookX, lookY, lookZ, 0.0, 0.0, 1.0);
        } else {
            // Orbit camera
            float eyeX = target.x() + distance * cos(pitch * M_PI / 180.0f) * cos(yaw * M_PI / 180.0f);
            float eyeY = target.y() + distance * cos(pitch * M_PI / 180.0f) * sin(yaw * M_PI / 180.0f);
            float eyeZ = target.z() + distance * sin(pitch * M_PI / 180.0f);
            gluLookAt(eyeX, eyeY, eyeZ, target.x(), target.y(), target.z(), 0.0, 0.0, 1.0);
        }

        for (int x = 0; x < gridSize; ++x) {
            for (int y = 0; y < gridSize; ++y) {
                for (int z = 0; z < gridSize; ++z) {
                    if (voxelGrid[index(x, y, z)].active) {
                        drawVoxel(x, y, z);
                    }
                }
            }
        }

        if (!firstPerson) drawPlayer();
    }

    void mousePressEvent(QMouseEvent *event) override {
        setFocus();
        lastMousePosition = event->pos();

        if (event->button() == Qt::RightButton && firstPerson) return; // Right drag = look in FP

        QVector3D origin, dir;
        if (firstPerson) {
            float eyeHeight = 1.2f;
            origin = QVector3D(player.pos.x(), player.pos.y(), player.pos.z() + eyeHeight);
            float lx = cos(fpPitch * M_PI / 180.0f) * cos(fpYaw * M_PI / 180.0f);
            float ly = cos(fpPitch * M_PI / 180.0f) * sin(fpYaw * M_PI / 180.0f);
            float lz = sin(fpPitch * M_PI / 180.0f);
            dir = QVector3D(lx, ly, lz).normalized();
        } else {
            origin = getRayOrigin(event->localPos());
            dir = getRayDirection(event->localPos());
        }

        QVector3D hitBlock, adjacentBlock;
        int hitTexId = -1;
        int hitFace = -1;
        bool hitVoxel = raycastVoxel(origin, dir, hitBlock, adjacentBlock, hitTexId, hitFace);

        bool hitGround = false;
        int gx = -1, gy = -1;

        if (!hitVoxel && !firstPerson) {
            if (std::abs(dir.z()) > 1e-5f) {
                float t = -origin.z() / dir.z();
                if (t > 0) {
                    QVector3D groundHit = origin + dir * t;
                    gx = floor(groundHit.x() + gridSize / 2.0f);
                    gy = floor(groundHit.y() + gridSize / 2.0f);
                    if (gx >= 0 && gx < gridSize && gy >= 0 && gy < gridSize) {
                        hitGround = true;
                    }
                }
            }
        }

        if (hitVoxel || hitGround) {
            if (event->button() == Qt::LeftButton) {
                // Check if clicked face is interactive
                if (hitVoxel && hitFace >= 0) {
                    int hx = hitBlock.x() + gridSize / 2.0f;
                    int hy = hitBlock.y() + gridSize / 2.0f;
                    int hz = hitBlock.z();
                    int idx = index(hx, hy, hz);

                    if (voxelGrid[idx].faces[hitFace].interactive) {
                        triggerInteraction(idx, hitFace);
                        return;
                    }
                }

                // Place block
                int ax, ay, az;
                if (hitVoxel) {
                    ax = adjacentBlock.x() + gridSize / 2.0f;
                    ay = adjacentBlock.y() + gridSize / 2.0f;
                    az = adjacentBlock.z();
                } else {
                    ax = gx; ay = gy; az = 0;
                }

                if (ax >= 0 && ax < gridSize && ay >= 0 && ay < gridSize && az >= 0 && az < gridSize) {
                    int idx = index(ax, ay, az);
                    if (!voxelGrid[idx].active) {
                        voxelGrid[idx].active = true;
                        for (int f = 0; f < 6; f++) {
                            voxelGrid[idx].faces[f].texId = currentTextureIdx;
                        }
                        voxelGrid[idx].type = currentTextureIdx;
                        voxelGrid[idx].age = 0;
                        update();
                    }
                }
            } else if (event->button() == Qt::RightButton) {
                if (hitVoxel) {
                    int hx = hitBlock.x() + gridSize / 2.0f;
                    int hy = hitBlock.y() + gridSize / 2.0f;
                    int hz = hitBlock.z();
                    if (hx >= 0 && hx < gridSize && hy >= 0 && hy < gridSize && hz >= 0 && hz < gridSize) {
                        voxelGrid[index(hx, hy, hz)].active = false;
                        update();
                    }
                }
            } else if (event->button() == Qt::MiddleButton) {
                if (hitVoxel && hitFace >= 0) {
                    int hx = hitBlock.x() + gridSize / 2.0f;
                    int hy = hitBlock.y() + gridSize / 2.0f;
                    int hz = hitBlock.z();
                    int idx = index(hx, hy, hz);
                    currentTextureIdx = voxelGrid[idx].faces[hitFace].texId;
                    updateInfoLabel();
                    update();
                }
            }
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        if (!firstPerson) {
            distance -= event->angleDelta().y() / 120.0f * 2.0f;
            if (distance < 2.0f) distance = 2.0f;
            if (distance > 50.0f) distance = 50.0f;
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        float dx = event->x() - lastMousePosition.x();
        float dy = event->y() - lastMousePosition.y();

        if (firstPerson) {
            // Mouse look in first person
            if (event->buttons() & Qt::RightButton) {
                fpYaw += dx * 0.3f;
                fpPitch -= dy * 0.3f;
                if (fpPitch > 89.0f) fpPitch = 89.0f;
                if (fpPitch < -89.0f) fpPitch = -89.0f;
                update();
            }
        } else {
            if (event->buttons() & Qt::MiddleButton) {
                yaw += dx * 0.5f;
                pitch += dy * 0.5f;
                if (pitch > 89.0f) pitch = 89.0f;
                if (pitch < -89.0f) pitch = -89.0f;
                update();
            }
        }
        lastMousePosition = event->pos();
    }

    void keyPressEvent(QKeyEvent *event) override {
        pressedKeys.insert(event->key());

        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6) {
            currentTextureIdx = event->key() - Qt::Key_1;
            updateInfoLabel();
            update();
        }

        if (event->key() == Qt::Key_F) {
            firstPerson = !firstPerson;
            if (firstPerson) {
                fpYaw = yaw + 180.0f; // Face the same direction
                fpPitch = 0.0f;
            }
            updateInfoLabel();
            update();
        }

        if (event->key() == Qt::Key_E) {
            assignInteractionToFace();
        }

        update();
    }

    void keyReleaseEvent(QKeyEvent *event) override {
        pressedKeys.remove(event->key());
    }

private:
    void triggerInteraction(int voxelIdx, int face) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastInteractionTime < 2000) return; // 2 second cooldown
        lastInteractionTime = now;

        FaceData& fd = voxelGrid[voxelIdx].faces[face];
        if (fd.interactive && fd.interactionType == 1) {
            qDebug() << "Triggering interaction:" << fd.url;
            QDesktopServices::openUrl(QUrl(fd.url));
        }
    }

    void assignInteractionToFace() {
        QVector3D origin, dir;
        if (firstPerson) {
            float eyeHeight = 1.2f;
            origin = QVector3D(player.pos.x(), player.pos.y(), player.pos.z() + eyeHeight);
            float lx = cos(fpPitch * M_PI / 180.0f) * cos(fpYaw * M_PI / 180.0f);
            float ly = cos(fpPitch * M_PI / 180.0f) * sin(fpYaw * M_PI / 180.0f);
            float lz = sin(fpPitch * M_PI / 180.0f);
            dir = QVector3D(lx, ly, lz).normalized();
        } else {
            origin = getRayOrigin(QPointF(width()/2.0, height()/2.0));
            dir = getRayDirection(QPointF(width()/2.0, height()/2.0));
        }

        QVector3D hitBlock, adjacentBlock;
        int hitTexId = -1, hitFace = -1;

        if (raycastVoxel(origin, dir, hitBlock, adjacentBlock, hitTexId, hitFace)) {
            int hx = hitBlock.x() + gridSize / 2.0f;
            int hy = hitBlock.y() + gridSize / 2.0f;
            int hz = hitBlock.z();

            if (hx >= 0 && hx < gridSize && hy >= 0 && hy < gridSize && hz >= 0 && hz < gridSize) {
                int idx = index(hx, hy, hz);

                bool ok;
                QString url = QInputDialog::getText(this, "Assign Movie/URL",
                    "Enter URL or file path for this face:",
                    QLineEdit::Normal, "https://youtube.com", &ok);

                if (ok && !url.isEmpty()) {
                    voxelGrid[idx].faces[hitFace].interactive = true;
                    voxelGrid[idx].faces[hitFace].interactionType = 1;
                    voxelGrid[idx].faces[hitFace].url = url;
                    voxelGrid[idx].faces[hitFace].texId = 5; // Poster texture
                    qDebug() << "Assigned interaction to face" << hitFace << ":" << url;
                    update();
                }
            }
        }
    }

    void updateInfoLabel() {
        if (infoLabel) {
            QStringList names = {"Dirt", "Stone", "Grass", "Brick", "Sand", "Poster"};
            QString mode = firstPerson ? "FIRST PERSON" : "ORBIT";
            infoLabel->setText(QString("[%1] Selected: %2 | F: Toggle View | E: Assign Movie | WASD: Move | Space: Jump | 1-6: Texture")
                .arg(mode).arg(names[currentTextureIdx]));
        }
    }

    GLuint createTexture(QColor baseColor, const QString& name) {
        QImage img(64, 64, QImage::Format_RGBA8888);
        img.fill(baseColor);

        for(int i=0; i<64; ++i) {
            for(int j=0; j<64; ++j) {
                QColor c = baseColor;
                if (name == "Brick") {
                    if (i % 16 == 0 || j % 32 == 0 || (j % 32 == 16 && i % 32 > 15))
                        c = QColor(200, 200, 200);
                } else if (name == "Poster") {
                    // Movie poster look - dark border, bright center
                    if (i < 4 || i > 59 || j < 4 || j > 59) c = QColor(20, 20, 80);
                    else if (i > 20 && i < 44 && j > 10 && j < 54) c = QColor(255, 255, 200);
                    else c = QColor(40, 40, 120);
                } else {
                    int noise = (qrand() % 40) - 20;
                    c = QColor(qBound(0, c.red() + noise, 255),
                               qBound(0, c.green() + noise, 255),
                               qBound(0, c.blue() + noise, 255));
                    if (i==0 || i==63 || j==0 || j==63) c = c.darker(150);
                }
                img.setPixelColor(i, j, c);
            }
        }
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return tex;
    }

    QVector3D getRayOrigin(const QPointF &mousePos) {
        float eyeX = target.x() + distance * cos(pitch * M_PI / 180.0f) * cos(yaw * M_PI / 180.0f);
        float eyeY = target.y() + distance * cos(pitch * M_PI / 180.0f) * sin(yaw * M_PI / 180.0f);
        float eyeZ = target.z() + distance * sin(pitch * M_PI / 180.0f);
        return QVector3D(eyeX, eyeY, eyeZ);
    }

    QVector3D getRayDirection(const QPointF &mousePos) {
        QVector3D eye = getRayOrigin(mousePos);
        QVector3D forward = (target - eye).normalized();
        QVector3D worldUp(0.0, 0.0, 1.0);
        QVector3D right = QVector3D::crossProduct(forward, worldUp).normalized();
        QVector3D up = QVector3D::crossProduct(right, forward).normalized();

        float w = width(); if (w <= 0) w = 800;
        float h = height(); if (h <= 0) h = 600;

        float ndcX = (2.0f * mousePos.x() / w) - 1.0f;
        float ndcY = 1.0f - (2.0f * mousePos.y() / h);
        float aspect = w / h;
        float tanFov = tan(60.0f * 0.5f * M_PI / 180.0f);

        QVector3D rayDirLocal(ndcX * aspect * tanFov, ndcY * tanFov, -1.0f);
        return (right * rayDirLocal.x() + up * rayDirLocal.y() + forward).normalized();
    }

    bool raycastVoxel(QVector3D origin, QVector3D dir, QVector3D &hitBlockWorld,
                      QVector3D &adjacentWorld, int &hitTexId, int &hitFace) {
        QVector3D gridOrigin(origin.x() + gridSize/2.0f, origin.y() + gridSize/2.0f, origin.z());
        gridOrigin += dir * 0.0001f;

        int x = floor(gridOrigin.x());
        int y = floor(gridOrigin.y());
        int z = floor(gridOrigin.z());

        int stepX = (dir.x() > 0) ? 1 : ((dir.x() < 0) ? -1 : 0);
        int stepY = (dir.y() > 0) ? 1 : ((dir.y() < 0) ? -1 : 0);
        int stepZ = (dir.z() > 0) ? 1 : ((dir.z() < 0) ? -1 : 0);

        float tDeltaX = (stepX != 0) ? std::abs(1.0f / dir.x()) : 1e30f;
        float tDeltaY = (stepY != 0) ? std::abs(1.0f / dir.y()) : 1e30f;
        float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / dir.z()) : 1e30f;

        float tMaxX = (stepX > 0) ? (x + 1.0f - gridOrigin.x()) * tDeltaX :
                      (stepX < 0) ? (gridOrigin.x() - x) * tDeltaX : 1e30f;
        float tMaxY = (stepY > 0) ? (y + 1.0f - gridOrigin.y()) * tDeltaY :
                      (stepY < 0) ? (gridOrigin.y() - y) * tDeltaY : 1e30f;
        float tMaxZ = (stepZ > 0) ? (z + 1.0f - gridOrigin.z()) * tDeltaZ :
                      (stepZ < 0) ? (gridOrigin.z() - z) * tDeltaZ : 1e30f;

        int prevX = x, prevY = y, prevZ = z;
        int lastAxis = -1;
        int lastDir = 0;

        for (int i = 0; i < 300; ++i) {
            if (x < -5 || x >= gridSize+5 || y < -5 || y >= gridSize+5 || z < -5 || z >= gridSize+5) break;

            if (x >= 0 && x < gridSize && y >= 0 && y < gridSize && z >= 0 && z < gridSize) {
                int idx = index(x, y, z);
                if (voxelGrid[idx].active) {
                    hitBlockWorld = QVector3D(x - gridSize/2.0f, y - gridSize/2.0f, z);
                    adjacentWorld = QVector3D(prevX - gridSize/2.0f, prevY - gridSize/2.0f, prevZ);
                    hitTexId = voxelGrid[idx].faces[0].texId;

                    // Determine which face was hit
                    if (lastAxis == 0) hitFace = (lastDir > 0) ? FACE_X_NEG : FACE_X_POS;
                    else if (lastAxis == 1) hitFace = (lastDir > 0) ? FACE_Y_NEG : FACE_Y_POS;
                    else if (lastAxis == 2) hitFace = (lastDir > 0) ? FACE_Z_NEG : FACE_Z_POS;
                    else hitFace = 0;

                    return true;
                }
            }

            prevX = x; prevY = y; prevZ = z;

            if (tMaxX < tMaxY) {
                if (tMaxX < tMaxZ) {
                    x += stepX; tMaxX += tDeltaX;
                    lastAxis = 0; lastDir = stepX;
                } else {
                    z += stepZ; tMaxZ += tDeltaZ;
                    lastAxis = 2; lastDir = stepZ;
                }
            } else {
                if (tMaxY < tMaxZ) {
                    y += stepY; tMaxY += tDeltaY;
                    lastAxis = 1; lastDir = stepY;
                } else {
                    z += stepZ; tMaxZ += tDeltaZ;
                    lastAxis = 2; lastDir = stepZ;
                }
            }
        }
        return false;
    }

    void updatePhysics() {
        QTime currentTime = QTime::currentTime();
        float dt = lastTime.msecsTo(currentTime) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        lastTime = currentTime;

        QVector3D moveForward, moveRight;

        if (firstPerson) {
            moveForward = QVector3D(cos(fpYaw * M_PI/180.0f), sin(fpYaw * M_PI/180.0f), 0);
            moveRight = QVector3D(-sin(fpYaw * M_PI/180.0f), cos(fpYaw * M_PI/180.0f), 0);
        } else {
            QVector3D eye = getRayOrigin(QPointF(width()/2.0, height()/2.0));
            moveForward = target - eye;
            moveForward.setZ(0);
            if (moveForward.length() > 0.001f) moveForward.normalize();
            moveRight = QVector3D::crossProduct(moveForward, QVector3D(0,0,1)).normalized();
        }

        QVector3D moveDir(0,0,0);
        if (pressedKeys.contains(Qt::Key_W)) moveDir += moveForward;
        if (pressedKeys.contains(Qt::Key_S)) moveDir -= moveForward;
        if (pressedKeys.contains(Qt::Key_A)) moveDir -= moveRight;
        if (pressedKeys.contains(Qt::Key_D)) moveDir += moveRight;

        if (moveDir.length() > 0) {
            moveDir.normalize();
            player.vel.setX(player.vel.x() + moveDir.x() * 25.0f * dt);
            player.vel.setY(player.vel.y() + moveDir.y() * 25.0f * dt);
        }

        player.vel.setZ(player.vel.z() - 25.0f * dt);

        float damp = 1.0f - 10.0f * dt;
        if (damp < 0) damp = 0;
        player.vel.setX(player.vel.x() * damp);
        player.vel.setY(player.vel.y() * damp);

        QVector3D nextPos = player.pos + player.vel * dt;
        float r = 0.4f;

        bool collided_wall = false;
        bool onGround = false;

        int minX = floor(nextPos.x() + gridSize/2.0f - r);
        int maxX = ceil(nextPos.x() + gridSize/2.0f + r);
        int minY = floor(nextPos.y() + gridSize/2.0f - r);
        int maxY = ceil(nextPos.y() + gridSize/2.0f + r);
        int minZ = floor(nextPos.z() - r);
        int maxZ = ceil(nextPos.z() + r);

        for(int bx = minX; bx <= maxX; ++bx) {
            for(int by = minY; by <= maxY; ++by) {
                for(int bz = minZ; bz <= maxZ; ++bz) {
                    if (bx>=0 && bx<gridSize && by>=0 && by<gridSize && bz>=0 && bz<gridSize) {
                        int idx = index(bx,by,bz);
                        if (voxelGrid[idx].active) {
                            float vMinX = bx - gridSize/2.0f, vMaxX = bx - gridSize/2.0f + 1.0f;
                            float vMinY = by - gridSize/2.0f, vMaxY = by - gridSize/2.0f + 1.0f;
                            float vMinZ = bz, vMaxZ = bz + 1.0f;

                            if (nextPos.x()+r > vMinX && nextPos.x()-r < vMaxX &&
                                nextPos.y()+r > vMinY && nextPos.y()-r < vMaxY &&
                                nextPos.z()+r > vMinZ && nextPos.z()-r < vMaxZ) {

                                QVector3D blockCenter(vMinX+0.5f, vMinY+0.5f, vMinZ+0.5f);
                                QVector3D diff = nextPos - blockCenter;

                                if (std::abs(diff.z()) > std::abs(diff.x()) && std::abs(diff.z()) > std::abs(diff.y())) {
                                    if (diff.z() > 0) {
                                        nextPos.setZ(vMaxZ + r);
                                        player.vel.setZ(0);
                                        onGround = true;
                                    } else {
                                        nextPos.setZ(vMinZ - r);
                                        player.vel.setZ(0);
                                    }
                                } else {
                                    collided_wall = true;

                                    // Determine which face was hit for interaction
                                    int hitFace = -1;
                                    if (std::abs(diff.x()) > std::abs(diff.y())) {
                                        hitFace = (diff.x() > 0) ? FACE_X_NEG : FACE_X_POS;
                                        nextPos.setX(diff.x() > 0 ? vMaxX + r : vMinX - r);
                                        player.vel.setX(0);
                                    } else {
                                        hitFace = (diff.y() > 0) ? FACE_Y_NEG : FACE_Y_POS;
                                        nextPos.setY(diff.y() > 0 ? vMaxY + r : vMinY - r);
                                        player.vel.setY(0);
                                    }

                                    // Check for interaction on wall collision
                                    if (hitFace >= 0 && voxelGrid[idx].faces[hitFace].interactive) {
                                        triggerInteraction(idx, hitFace);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (pressedKeys.contains(Qt::Key_Space) && onGround) {
            player.vel.setZ(12.0f);
        }

        if (nextPos.z() < -10.0f) { nextPos.setZ(3.0f); player.vel.setZ(0); }
        player.pos = nextPos;
    }

    void drawFace(int face) {
        glBegin(GL_QUADS);
        switch(face) {
            case FACE_X_NEG: // Left (-X)
                glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(-0.5,-0.5,0.5);
                glTexCoord2f(1,1); glVertex3f(-0.5,0.5,0.5);  glTexCoord2f(0,1); glVertex3f(-0.5,0.5,-0.5);
                break;
            case FACE_X_POS: // Right (+X)
                glTexCoord2f(0,0); glVertex3f(0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,0.5);
                glTexCoord2f(1,1); glVertex3f(0.5,0.5,0.5);   glTexCoord2f(0,1); glVertex3f(0.5,0.5,-0.5);
                break;
            case FACE_Y_NEG: // Back (-Y)
                glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,-0.5);
                glTexCoord2f(1,1); glVertex3f(0.5,0.5,-0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,0.5,-0.5);
                break;
            case FACE_Y_POS: // Front (+Y)
                glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,0.5);
                glTexCoord2f(1,1); glVertex3f(0.5,0.5,0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,0.5,0.5);
                break;
            case FACE_Z_NEG: // Bottom (-Z)
                glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,-0.5);
                glTexCoord2f(1,1); glVertex3f(0.5,-0.5,0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,-0.5,0.5);
                break;
            case FACE_Z_POS: // Top (+Z)
                glTexCoord2f(0,0); glVertex3f(-0.5,0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,0.5,-0.5);
                glTexCoord2f(1,1); glVertex3f(0.5,0.5,0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,0.5,0.5);
                break;
        }
        glEnd();
    }

    void drawVoxel(int x, int y, int z) {
        int idx = index(x, y, z);

        glPushMatrix();
        glTranslatef(x - gridSize/2.0f, y - gridSize/2.0f, z);
        glEnable(GL_TEXTURE_2D);

        for (int f = 0; f < 6; f++) {
            FaceData& fd = voxelGrid[idx].faces[f];
            glBindTexture(GL_TEXTURE_2D, textures[fd.texId]);

            if (fd.interactive) {
                glColor3f(0.7f, 0.8f, 1.0f); // Slight blue tint for interactive
            } else {
                glColor3f(1.0f, 1.0f, 1.0f);
            }
            drawFace(f);
        }

        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }

    void drawPlayer() {
        glPushMatrix();
        glTranslatef(player.pos.x(), player.pos.y(), player.pos.z());
        glColor3f(1.0f, 1.0f, 0.0f);
        glBegin(GL_QUADS);
        float r = 0.4f;
        glVertex3f(-r,-r,r); glVertex3f(r,-r,r); glVertex3f(r,r,r); glVertex3f(-r,r,r);
        glVertex3f(-r,-r,-r); glVertex3f(r,-r,-r); glVertex3f(r,r,-r); glVertex3f(-r,r,-r);
        glVertex3f(-r,-r,-r); glVertex3f(-r,-r,r); glVertex3f(-r,r,r); glVertex3f(-r,r,-r);
        glVertex3f(r,-r,-r); glVertex3f(r,-r,r); glVertex3f(r,r,r); glVertex3f(r,r,-r);
        glVertex3f(-r,r,-r); glVertex3f(r,r,-r); glVertex3f(r,r,r); glVertex3f(-r,r,r);
        glVertex3f(-r,-r,-r); glVertex3f(r,-r,-r); glVertex3f(r,-r,r); glVertex3f(-r,-r,r);
        glEnd();
        glPopMatrix();
    }

    int index(int x, int y, int z) const { return x + gridSize * (y + gridSize * z); }

    int gridSize;
    float distance, yaw, pitch;
    float fpYaw, fpPitch;
    bool firstPerson;
    int currentTextureIdx;
    QVector3D target;
    QSet<int> pressedKeys;
    QPoint lastMousePosition;
    std::vector<Voxel> voxelGrid;
    std::vector<GLuint> textures;
    Entity player;
    QTime lastTime;
    QLabel* infoLabel = nullptr;
    qint64 lastInteractionTime;
};

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    QMainWindow w;

    QWidget* central = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);

    OpenGLWidget* glWidget = new OpenGLWidget();
    layout->addWidget(glWidget);

    QLabel* lbl = new QLabel("[ORBIT] Selected: Dirt | F: Toggle View | E: Assign Movie | WASD: Move | Space: Jump | 1-6: Texture");
    lbl->setStyleSheet("color: yellow; background: rgba(0,0,0,180); padding: 5px; font-weight: bold;");
    layout->addWidget(lbl);

    glWidget->setInfoLabel(lbl);

    w.setCentralWidget(central);
    w.resize(900, 700);
    w.show();
    return a.exec();
}

#include "main.moc"
