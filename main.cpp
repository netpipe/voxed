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
#include <vector>
#include <cmath>
#include <QDebug>
#include <OpenGL/glu.h>
#include <GLUT/glut.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Voxel {
    bool active = false;
    int texId = 0;

    // Advanced node properties
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
        : QOpenGLWidget(parent), gridSize(20), distance(25.0f), yaw(45.0f), pitch(35.0f), currentTextureIdx(0) {

        // CRITICAL FIX: Allows the widget to receive WASD and 1-5 key presses
        setFocusPolicy(Qt::StrongFocus);

        voxelGrid.resize(gridSize * gridSize * gridSize);
        target = QVector3D(0.0f, 0.0f, 5.0f);

        player.pos = QVector3D(0.0f, 0.0f, 3.0f);
        player.vel = QVector3D(0.0f, 0.0f, 0.0f);
        lastTime = QTime::currentTime();

        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            updatePhysics();
            update();
        });
        timer->start(16); // ~60 FPS
    }

    void setInfoLabel(QLabel* lbl) { infoLabel = lbl; }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.5f, 0.7f, 1.0f, 1.0f);

        // Initialize Procedural Textures
        textures.push_back(createTexture(QColor(139, 69, 19), "Dirt"));
        textures.push_back(createTexture(QColor(128, 128, 128), "Stone"));
        textures.push_back(createTexture(QColor(34, 139, 34), "Grass"));
        textures.push_back(createTexture(QColor(178, 34, 34), "Brick"));
        textures.push_back(createTexture(QColor(240, 220, 150), "Sand"));
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(45.0, static_cast<double>(w) / h, 0.1, 100.0);
        glMatrixMode(GL_MODELVIEW);
    }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();

        float eyeX = target.x() + distance * cos(pitch * M_PI / 180.0f) * cos(yaw * M_PI / 180.0f);
        float eyeY = target.y() + distance * cos(pitch * M_PI / 180.0f) * sin(yaw * M_PI / 180.0f);
        float eyeZ = target.z() + distance * sin(pitch * M_PI / 180.0f);

        gluLookAt(eyeX, eyeY, eyeZ, target.x(), target.y(), target.z(), 0.0, 0.0, 1.0);

        for (int x = 0; x < gridSize; ++x) {
            for (int y = 0; y < gridSize; ++y) {
                for (int z = 0; z < gridSize; ++z) {
                    if (voxelGrid[index(x, y, z)].active) {
                        drawVoxel(x, y, z);
                    }
                }
            }
        }
        drawPlayer();
    }

    void mousePressEvent(QMouseEvent *event) override {
        setFocus(); // Ensure we get keyboard focus when clicking

        QVector3D origin = getRayOrigin(event->localPos());
        QVector3D dir = getRayDirection(event->localPos());

        QVector3D hitBlock, adjacentBlock;
        int hitTexId = -1;
        bool hitVoxel = raycastVoxel(origin, dir, hitBlock, adjacentBlock, hitTexId);

        bool hitGround = false;
        int gx = -1, gy = -1;

        if (!hitVoxel) {
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
                        voxelGrid[idx].texId = currentTextureIdx;
                        voxelGrid[idx].type = currentTextureIdx;
                        voxelGrid[idx].weight = 1.0f;
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
                if (hitVoxel) {
                    currentTextureIdx = hitTexId;
                    updateInfoLabel();
                    qDebug() << "Picked block type:" << currentTextureIdx;
                    update();
                }
            }
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        distance -= event->angleDelta().y() / 120.0f * 2.0f;
        if (distance < 2.0f) distance = 2.0f;
        if (distance > 50.0f) distance = 50.0f;
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons() & Qt::MiddleButton) {
            float dx = event->x() - lastMousePosition.x();
            float dy = event->y() - lastMousePosition.y();

            yaw += dx * 0.5f;
            pitch += dy * 0.5f;

            if (pitch > 89.0f) pitch = 89.0f;
            if (pitch < -89.0f) pitch = -89.0f;

            update();
        }
        lastMousePosition = event->pos();
    }

    void keyPressEvent(QKeyEvent *event) override {
        pressedKeys.insert(event->key());
        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5) {
            currentTextureIdx = event->key() - Qt::Key_1;
            updateInfoLabel();
            qDebug() << "Selected Texture ID:" << currentTextureIdx;
            update();
        }
    }

    void keyReleaseEvent(QKeyEvent *event) override {
        pressedKeys.remove(event->key());
    }

private:
    void updateInfoLabel() {
        if (infoLabel) {
            QStringList names = {"Dirt", "Stone", "Grass", "Brick", "Sand"};
            infoLabel->setText("Selected: " + names[currentTextureIdx] + " | Left: Place | Right: Remove | WASD: Move | Space: Jump | 1-5: Texture");
        }
    }

    GLuint createTexture(QColor baseColor, const QString& name) {
        QImage img(64, 64, QImage::Format_RGBA8888);
        img.fill(baseColor);

        for(int i=0; i<64; ++i) {
            for(int j=0; j<64; ++j) {
                QColor c = baseColor;
                if (name == "Brick") {
                    if (i % 16 == 0 || j % 32 == 0 || (j % 32 == 16 && i % 32 > 15)) {
                        c = QColor(200, 200, 200);
                    }
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

        float w = width();
        float h = height();
        if (w <= 0) w = 800;
        if (h <= 0) h = 600;

        float ndcX = (2.0f * mousePos.x() / w) - 1.0f;
        float ndcY = 1.0f - (2.0f * mousePos.y() / h);
        float aspect = w / h;
        float tanFov = tan(45.0f * 0.5f * M_PI / 180.0f);

        QVector3D rayDirLocal(ndcX * aspect * tanFov, ndcY * tanFov, -1.0f);
        return (right * rayDirLocal.x() + up * rayDirLocal.y() + forward).normalized();
    }

    bool raycastVoxel(QVector3D origin, QVector3D dir, QVector3D &hitBlockWorld, QVector3D &adjacentWorld, int &hitTexId) {
        QVector3D gridOrigin = QVector3D(origin.x() + gridSize / 2.0f, origin.y() + gridSize / 2.0f, origin.z());
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

        float tMaxX, tMaxY, tMaxZ;
        if (stepX > 0) tMaxX = (x + 1.0f - gridOrigin.x()) * tDeltaX;
        else if (stepX < 0) tMaxX = (gridOrigin.x() - x) * tDeltaX;
        else tMaxX = 1e30f;

        if (stepY > 0) tMaxY = (y + 1.0f - gridOrigin.y()) * tDeltaY;
        else if (stepY < 0) tMaxY = (gridOrigin.y() - y) * tDeltaY;
        else tMaxY = 1e30f;

        if (stepZ > 0) tMaxZ = (z + 1.0f - gridOrigin.z()) * tDeltaZ;
        else if (stepZ < 0) tMaxZ = (gridOrigin.z() - z) * tDeltaZ;
        else tMaxZ = 1e30f;

        int prevX = x, prevY = y, prevZ = z;

        for (int i = 0; i < 300; ++i) {
            if (x < -5 || x >= gridSize + 5 || y < -5 || y >= gridSize + 5 || z < -5 || z >= gridSize + 5) break;

            if (x >= 0 && x < gridSize && y >= 0 && y < gridSize && z >= 0 && z < gridSize) {
                int idx = index(x, y, z);
                if (voxelGrid[idx].active) {
                    hitBlockWorld = QVector3D(x - gridSize/2.0f, y - gridSize/2.0f, z);
                    adjacentWorld = QVector3D(prevX - gridSize/2.0f, prevY - gridSize/2.0f, prevZ);
                    hitTexId = voxelGrid[idx].texId;
                    return true;
                }
            }

            prevX = x; prevY = y; prevZ = z;

            if (tMaxX < tMaxY) {
                if (tMaxX < tMaxZ) { x += stepX; tMaxX += tDeltaX; }
                else { z += stepZ; tMaxZ += tDeltaZ; }
            } else {
                if (tMaxY < tMaxZ) { y += stepY; tMaxY += tDeltaY; }
                else { z += stepZ; tMaxZ += tDeltaZ; }
            }
        }
        return false;
    }

    void updatePhysics() {
        QTime currentTime = QTime::currentTime();
        float dt = lastTime.msecsTo(currentTime) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        lastTime = currentTime;

        // Calculate movement vectors relative to the camera
        QVector3D eye = getRayOrigin(QPointF(width()/2.0, height()/2.0));
        QVector3D forward = target - eye;
        forward.setZ(0); // Project onto XY plane for horizontal movement
        if (forward.length() > 0.001f) forward.normalize();
        QVector3D right = QVector3D::crossProduct(forward, QVector3D(0,0,1)).normalized();

        QVector3D moveDir(0,0,0);
        if (pressedKeys.contains(Qt::Key_W)) moveDir = moveDir + forward;
        if (pressedKeys.contains(Qt::Key_S)) moveDir = moveDir - forward;
        if (pressedKeys.contains(Qt::Key_A)) moveDir = moveDir - right;
        if (pressedKeys.contains(Qt::Key_D)) moveDir = moveDir + right;

        if (moveDir.length() > 0) {
            moveDir.normalize();
            player.vel.setX(player.vel.x() + moveDir.x() * 25.0f * dt);
            player.vel.setY(player.vel.y() + moveDir.y() * 25.0f * dt);
        }

        player.vel.setZ(player.vel.z() - 25.0f * dt); // gravity

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

        for(int x = minX; x <= maxX; ++x) {
            for(int y = minY; y <= maxY; ++y) {
                for(int z = minZ; z <= maxZ; ++z) {
                    if (x>=0 && x<gridSize && y>=0 && y<gridSize && z>=0 && z<gridSize) {
                        int idx = index(x,y,z);
                        if (voxelGrid[idx].active) {
                            float vMinX = x - gridSize/2.0f, vMaxX = x - gridSize/2.0f + 1.0f;
                            float vMinY = y - gridSize/2.0f, vMaxY = y - gridSize/2.0f + 1.0f;
                            float vMinZ = z, vMaxZ = z + 1.0f;

                            if (nextPos.x() + r > vMinX && nextPos.x() - r < vMaxX &&
                                nextPos.y() + r > vMinY && nextPos.y() - r < vMaxY &&
                                nextPos.z() + r > vMinZ && nextPos.z() - r < vMaxZ) {

                                QVector3D blockCenter(vMinX + 0.5f, vMinY + 0.5f, vMinZ + 0.5f);
                                QVector3D diff = nextPos - blockCenter;

                                // Collision logic for floors/ceilings vs walls
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
                                    int wallType = voxelGrid[idx].getType();
                                    qDebug() << "Entity touched a wall! Block type:" << wallType << "Age:" << voxelGrid[idx].getAge();

                                    if (std::abs(diff.x()) > std::abs(diff.y())) {
                                        nextPos.setX(diff.x() > 0 ? vMaxX + r : vMinX - r);
                                        player.vel.setX(0);
                                    } else {
                                        nextPos.setY(diff.y() > 0 ? vMaxY + r : vMinY - r);
                                        player.vel.setY(0);
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

    void drawVoxel(int x, int y, int z) {
        int idx = index(x, y, z);
        int type = voxelGrid[idx].texId;

        glPushMatrix();
        glTranslatef(x - gridSize / 2.0f, y - gridSize / 2.0f, z);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, textures[type]);
        glColor3f(1.0, 1.0, 1.0); // Pure white so textures aren't tinted

        glBegin(GL_QUADS);
        // Front
        glTexCoord2f(0,0); glVertex3f(-0.5, -0.5, 0.5); glTexCoord2f(1,0); glVertex3f( 0.5, -0.5, 0.5);
        glTexCoord2f(1,1); glVertex3f( 0.5, 0.5, 0.5); glTexCoord2f(0,1); glVertex3f(-0.5, 0.5, 0.5);
        // Back
        glTexCoord2f(0,0); glVertex3f(-0.5, -0.5, -0.5); glTexCoord2f(1,0); glVertex3f( 0.5, -0.5, -0.5);
        glTexCoord2f(1,1); glVertex3f( 0.5, 0.5, -0.5); glTexCoord2f(0,1); glVertex3f(-0.5, 0.5, -0.5);
        // Left
        glTexCoord2f(0,0); glVertex3f(-0.5, -0.5, -0.5); glTexCoord2f(1,0); glVertex3f(-0.5, -0.5, 0.5);
        glTexCoord2f(1,1); glVertex3f(-0.5, 0.5, 0.5); glTexCoord2f(0,1); glVertex3f(-0.5, 0.5, -0.5);
        // Right
        glTexCoord2f(0,0); glVertex3f(0.5, -0.5, -0.5); glTexCoord2f(1,0); glVertex3f(0.5, -0.5, 0.5);
        glTexCoord2f(1,1); glVertex3f(0.5, 0.5, 0.5); glTexCoord2f(0,1); glVertex3f(0.5, 0.5, -0.5);
        // Top
        glTexCoord2f(0,0); glVertex3f(-0.5, 0.5, -0.5); glTexCoord2f(1,0); glVertex3f( 0.5, 0.5, -0.5);
        glTexCoord2f(1,1); glVertex3f( 0.5, 0.5, 0.5); glTexCoord2f(0,1); glVertex3f(-0.5, 0.5, 0.5);
        // Bottom
        glTexCoord2f(0,0); glVertex3f(-0.5, -0.5, -0.5); glTexCoord2f(1,0); glVertex3f( 0.5, -0.5, -0.5);
        glTexCoord2f(1,1); glVertex3f( 0.5, -0.5, 0.5); glTexCoord2f(0,1); glVertex3f(-0.5, -0.5, 0.5);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }

    void drawPlayer() {
        glPushMatrix();
        glTranslatef(player.pos.x(), player.pos.y(), player.pos.z());
        glColor3f(1.0f, 1.0f, 0.0f); // Yellow player
        glBegin(GL_QUADS);
        float r = 0.4f;
        // Front
        glVertex3f(-r, -r, r); glVertex3f(r, -r, r); glVertex3f(r, r, r); glVertex3f(-r, r, r);
        // Back
        glVertex3f(-r, -r, -r); glVertex3f(r, -r, -r); glVertex3f(r, r, -r); glVertex3f(-r, r, -r);
        // Left
        glVertex3f(-r, -r, -r); glVertex3f(-r, -r, r); glVertex3f(-r, r, r); glVertex3f(-r, r, -r);
        // Right
        glVertex3f(r, -r, -r); glVertex3f(r, -r, r); glVertex3f(r, r, r); glVertex3f(r, r, -r);
        // Top
        glVertex3f(-r, r, -r); glVertex3f(r, r, -r); glVertex3f(r, r, r); glVertex3f(-r, r, r);
        // Bottom
        glVertex3f(-r, -r, -r); glVertex3f(r, -r, -r); glVertex3f(r, -r, r); glVertex3f(-r, -r, r);
        glEnd();
        glPopMatrix();
    }

    int index(int x, int y, int z) const { return x + gridSize * (y + gridSize * z); }

    int gridSize;
    float distance, yaw, pitch;
    int currentTextureIdx;
    QVector3D target;
    QSet<int> pressedKeys;
    QPoint lastMousePosition;
    std::vector<Voxel> voxelGrid;
    std::vector<GLuint> textures;
    Entity player;
    QTime lastTime;
    QLabel* infoLabel = nullptr;
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

    QLabel* lbl = new QLabel("Selected: Dirt | Left: Place | Right: Remove | Middle Click: Pick | Middle Drag: Rotate | Scroll: Zoom | WASD: Move | Space: Jump | 1-5: Texture");
    lbl->setStyleSheet("color: yellow; background: rgba(0,0,0,150); padding: 5px; font-weight: bold;");
    layout->addWidget(lbl);

    glWidget->setInfoLabel(lbl);

    w.setCentralWidget(central);
    w.resize(800, 600);
    w.show();
    return a.exec();
}

#include "main.moc"
