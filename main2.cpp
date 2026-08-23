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

    // New properties for advanced logic
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
        voxelGrid.resize(gridSize * gridSize * gridSize);
        target = QVector3D(0.0f, 0.0f, 5.0f); // Look at the center of the grid

        player.pos = QVector3D(0.0f, 0.0f, 3.0f);
        player.vel = QVector3D(0.0f, 0.0f, 0.0f);

        // Physics loop
        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            updatePhysics();
            update();
        });
        timer->start(16); // ~60 FPS
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.5f, 0.7f, 1.0f, 1.0f); // Sky blue

        // Generate procedural textures
        textures.push_back(createTexture(QColor(139, 69, 19)));  // Dirt
        textures.push_back(createTexture(QColor(128, 128, 128))); // Stone
        textures.push_back(createTexture(QColor(34, 139, 34)));   // Grass
        textures.push_back(createTexture(QColor(178, 34, 34)));   // Red Brick
        textures.push_back(createTexture(QColor(240, 240, 240))); // White
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

        // Orbit Camera
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
        QVector3D origin = unproject(event->pos(), 0.001f);
        QVector3D farPlane = unproject(event->pos(), 0.999f);
        QVector3D dir = (farPlane - origin).normalized();

        QVector3D hitBlock, adjacentBlock;
        int hitTexId = -1;
        bool hitVoxel = raycastVoxel(origin, dir, hitBlock, adjacentBlock, hitTexId);

        bool hitGround = false;
        int gx = -1, gy = -1;

        // Fallback: If no voxel was hit, check if we clicked the Z=0 floor plane
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
                    ax = gx;
                    ay = gy;
                    az = 0; // Place on the floor
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
                    qDebug() << "Picked texture ID:" << currentTextureIdx;
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
        }
        update();
    }

    void keyReleaseEvent(QKeyEvent *event) override {
        pressedKeys.remove(event->key());
    }

private:
    GLuint createTexture(const QColor &baseColor) {
        QImage img(16, 16, QImage::Format_RGBA8888);
        img.fill(baseColor);
        for(int i=0; i<16; ++i) {
            for(int j=0; j<16; ++j) {
                if (i==0 || i==15 || j==0 || j==15) img.setPixelColor(i, j, baseColor.darker(150));
                else if ((i + j) % 4 == 0) img.setPixelColor(i, j, baseColor.lighter(120));
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

    QVector3D unproject(const QPointF &mousePos, float depth) {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        GLdouble modelview[16]; glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
        GLdouble projection[16]; glGetDoublev(GL_PROJECTION_MATRIX, projection);
        GLdouble posX, posY, posZ;
        gluUnProject(mousePos.x(), viewport[3] - mousePos.y() - 1, depth, modelview, projection, viewport, &posX, &posY, &posZ);
        return QVector3D(posX, posY, posZ);
    }

    // Robust DDA Raycaster
    bool raycastVoxel(QVector3D origin, QVector3D dir, QVector3D &hitBlockWorld, QVector3D &adjacentWorld, int &hitTexId) {
        QVector3D gridOrigin = QVector3D(origin.x() + gridSize / 2.0f, origin.y() + gridSize / 2.0f, origin.z());
        gridOrigin += dir * 0.0001f; // Shift slightly to avoid boundary bugs

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
                if (tMaxX < tMaxZ) {
                    x += stepX;
                    tMaxX += tDeltaX;
                } else {
                    z += stepZ;
                    tMaxZ += tDeltaZ;
                }
            } else {
                if (tMaxY < tMaxZ) {
                    y += stepY;
                    tMaxY += tDeltaY;
                } else {
                    z += stepZ;
                    tMaxZ += tDeltaZ;
                }
            }
        }
        return false;
    }

    void updatePhysics() {
        QVector3D forward(cos(yaw * M_PI/180.0f), sin(yaw * M_PI/180.0f), 0);
        QVector3D right(-sin(yaw * M_PI/180.0f), cos(yaw * M_PI/180.0f), 0);

        QVector3D moveDir(0,0,0);
        if (pressedKeys.contains(Qt::Key_W)) moveDir = moveDir + forward;
        if (pressedKeys.contains(Qt::Key_S)) moveDir = moveDir - forward;
        if (pressedKeys.contains(Qt::Key_A)) moveDir = moveDir - right;
        if (pressedKeys.contains(Qt::Key_D)) moveDir = moveDir + right;

        if (moveDir.length() > 0) {
            moveDir.normalize();
            player.vel.setX(player.vel.x() + moveDir.x() * 0.005f);
            player.vel.setY(player.vel.y() + moveDir.y() * 0.005f);
        }

        player.vel.setZ(player.vel.z() - 0.002f); // gravity
        player.vel.setX(player.vel.x() * 0.9f);   // damping
        player.vel.setY(player.vel.y() * 0.9f);
        player.vel.setZ(player.vel.z() * 0.95f);

        QVector3D nextPos = player.pos + player.vel;
        float r = 0.4f;

        bool collided_wall = false;

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

                                // Smart Collision: Detect if we hit a wall or a floor
                                if (std::abs(diff.z()) > std::abs(diff.x()) && std::abs(diff.z()) > std::abs(diff.y())) {
                                    // Floor or Ceiling (Z dominant)
                                    if (diff.z() > 0) {
                                        nextPos.setZ(vMaxZ + r);
                                        player.vel.setZ(0);
                                    } else {
                                        nextPos.setZ(vMinZ - r);
                                        player.vel.setZ(0);
                                    }
                                } else {
                                    // Wall (X or Y dominant)
                                    collided_wall = true;
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

        if (collided_wall) qDebug() << "Entity touched a non-floor texture (Wall)!";
        if (nextPos.z() < -10.0f) { nextPos.setZ(3.0f); player.vel.setZ(0); }
        player.pos = nextPos;
    }

    void drawVoxel(int x, int y, int z) {
        glPushMatrix();
        glTranslatef(x - gridSize / 2.0f, y - gridSize / 2.0f, z);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, textures[voxelGrid[index(x, y, z)].texId]);
        glColor3f(1.0, 1.0, 1.0);
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
        glColor3f(1.0f, 1.0f, 0.0f);
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

    QLabel* lbl = new QLabel("Left: Place | Right: Remove | Middle Click: Pick Texture | Middle Drag: Rotate | Scroll: Zoom | WASD: Move | 1-5: Change Texture");
    lbl->setStyleSheet("color: yellow; background: rgba(0,0,0,150); padding: 5px;");
    layout->addWidget(lbl);

    w.setCentralWidget(central);
    w.resize(800, 600);
    w.show();
    return a.exec();
}

#include "main.moc"
