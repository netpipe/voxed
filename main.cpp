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
#include <QDateTime>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <cmath>
#include <QDebug>
#include <OpenGL/glu.h>
#include <GLUT/glut.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    FACE_X_NEG = 0, FACE_X_POS = 1,
    FACE_Y_NEG = 2, FACE_Y_POS = 3,
    FACE_Z_NEG = 4, FACE_Z_POS = 5
};

struct FaceData {
    int texId = 0;
    bool interactive = false;
    int interactionType = 0; // 0=none, 1=url/movie
    QString url;
    QString imagePath;       // custom poster image (reloaded on load)
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

struct Entity { QVector3D pos, vel; };

struct TargetInfo {
    bool valid = false;
    QVector3D hitBlock;       // center of block you're looking at
    QVector3D adjacentBlock;  // where a new block would be placed
    int voxelIdx = -1;
    int face = -1;
    int texId = -1;
};

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    OpenGLWidget(QWidget *parent = nullptr)
        : QOpenGLWidget(parent), gridSize(20), distance(25.0f),
          yaw(45.0f), pitch(35.0f), fpYaw(-90.0f), fpPitch(0.0f),
          currentTextureIdx(0), firstPerson(false) {

        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);

        voxelGrid.resize(gridSize * gridSize * gridSize);
        target = QVector3D(0.0f, 0.0f, 5.0f);

        player.pos = QVector3D(0.0f, 0.0f, 2.0f);
        player.vel = QVector3D(0.0f, 0.0f, 0.0f);
        lastTime = QTime::currentTime();
        lastInteractionTime = 0;
        lastMousePosition = QPoint(400, 300);

        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            updatePhysics();
            update();
        });
        timer->start(16);
    }

    void setInfoLabel(QLabel* lbl) { infoLabel = lbl; }

    // ---------- SAVE / LOAD ----------
    void saveWorld() {
        QString path = QFileDialog::getSaveFileName(this, "Save World", "world.json", "JSON (*.json)");
        if (path.isEmpty()) return;

        QJsonObject root;
        root["gridSize"] = gridSize;
        root["playerX"] = (double)player.pos.x();
        root["playerY"] = (double)player.pos.y();
        root["playerZ"] = (double)player.pos.z();

        QJsonArray voxArr;
        for (int x = 0; x < gridSize; ++x)
        for (int y = 0; y < gridSize; ++y)
        for (int z = 0; z < gridSize; ++z) {
            int idx = index(x, y, z);
            if (!voxelGrid[idx].active) continue;

            QJsonObject vo;
            vo["x"] = x; vo["y"] = y; vo["z"] = z;
            vo["age"] = voxelGrid[idx].age;
            vo["type"] = voxelGrid[idx].type;
            vo["weight"] = (double)voxelGrid[idx].weight;
            vo["affector"] = (double)voxelGrid[idx].affectorValue;

            QJsonArray facesArr;
            for (int f = 0; f < 6; ++f) {
                const FaceData& fd = voxelGrid[idx].faces[f];
                QJsonObject fo;
                fo["tex"] = fd.texId;
                fo["interactive"] = fd.interactive;
                fo["itype"] = fd.interactionType;
                fo["url"] = fd.url;
                fo["img"] = fd.imagePath;
                facesArr.append(fo);
            }
            vo["faces"] = facesArr;
            voxArr.append(vo);
        }
        root["voxels"] = voxArr;

        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
            qDebug() << "World saved:" << path << "(" << voxArr.size() << "voxels)";
        }
    }

    void loadWorld() {
        QString path = QFileDialog::getOpenFileName(this, "Load World", "", "JSON (*.json)");
        if (path.isEmpty()) return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject root = doc.object();

        int gs = root["gridSize"].toInt(20);
        gridSize = gs;
        voxelGrid.assign(gridSize * gridSize * gridSize, Voxel());

        player.pos = QVector3D(root["playerX"].toDouble(0), root["playerY"].toDouble(0), root["playerZ"].toDouble(2));
        player.vel = QVector3D(0,0,0);

        makeCurrent(); // GL needed for texture creation
        QJsonArray voxArr = root["voxels"].toArray();
        for (const QJsonValue& v : voxArr) {
            QJsonObject vo = v.toObject();
            int x = vo["x"].toInt(), y = vo["y"].toInt(), z = vo["z"].toInt();
            if (x < 0 || x >= gridSize || y < 0 || y >= gridSize || z < 0 || z >= gridSize) continue;

            Voxel& vx = voxelGrid[index(x, y, z)];
            vx.active = true;
            vx.age = vo["age"].toInt(0);
            vx.type = vo["type"].toInt(0);
            vx.weight = (float)vo["weight"].toDouble(1.0);
            vx.affectorValue = (float)vo["affector"].toDouble(0.0);

            QJsonArray facesArr = vo["faces"].toArray();
            for (int f = 0; f < 6 && f < facesArr.size(); ++f) {
                QJsonObject fo = facesArr[f].toObject();
                FaceData& fd = vx.faces[f];
                fd.texId = fo["tex"].toInt(0);
                fd.interactive = fo["interactive"].toBool(false);
                fd.interactionType = fo["itype"].toInt(0);
                fd.url = fo["url"].toString();
                fd.imagePath = fo["img"].toString();
                if (!fd.imagePath.isEmpty()) {
                    fd.texId = getTextureForPath(fd.imagePath); // rebuild custom texture
                }
            }
        }
        doneCurrent();
        update();
        qDebug() << "World loaded:" << path << "(" << voxArr.size() << "voxels)";
    }

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
        textures.push_back(createTexture(QColor(50, 50, 200), "Poster"));
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
            float ex = player.pos.x(), ey = player.pos.y(), ez = player.pos.z() + 1.2f;
            float lx = ex + cos(fpPitch*M_PI/180.0f) * cos(fpYaw*M_PI/180.0f);
            float ly = ey + cos(fpPitch*M_PI/180.0f) * sin(fpYaw*M_PI/180.0f);
            float lz = ez + sin(fpPitch*M_PI/180.0f);
            gluLookAt(ex, ey, ez, lx, ly, lz, 0.0, 0.0, 1.0);
        } else {
            float eyeX = target.x() + distance * cos(pitch*M_PI/180.0f) * cos(yaw*M_PI/180.0f);
            float eyeY = target.y() + distance * cos(pitch*M_PI/180.0f) * sin(yaw*M_PI/180.0f);
            float eyeZ = target.z() + distance * sin(pitch*M_PI/180.0f);
            gluLookAt(eyeX, eyeY, eyeZ, target.x(), target.y(), target.z(), 0.0, 0.0, 1.0);
        }

        for (int x = 0; x < gridSize; ++x)
        for (int y = 0; y < gridSize; ++y)
        for (int z = 0; z < gridSize; ++z)
            if (voxelGrid[index(x, y, z)].active) drawVoxel(x, y, z);

        if (!firstPerson) drawPlayer();

        // ---- selection feedback ----
        QPointF aim = firstPerson ? QPointF(width()/2.0, height()/2.0)
                                  : QPointF(lastMousePosition);
        TargetInfo target = getTarget(aim);
        if (target.valid) drawTargetHighlight(target);
        if (firstPerson) drawCrosshair();
    }

    void mousePressEvent(QMouseEvent *event) override {
        setFocus();
        lastMousePosition = event->pos();

        if (firstPerson && event->button() == Qt::RightButton) {
            rightPressPos = event->pos();
            rightDragged = false;
            return; // right-drag = look; release without drag = delete
        }

        QVector3D origin, dir;
        getViewRay(event->localPos(), origin, dir);

        QVector3D hitBlock, adjacentBlock;
        int hitTexId = -1, hitFace = -1;
        bool hitVoxel = raycastVoxel(origin, dir, hitBlock, adjacentBlock, hitTexId, hitFace);

        bool hitGround = false;
        int gx = -1, gy = -1;
        if (!hitVoxel && !firstPerson && std::abs(dir.z()) > 1e-5f) {
            float t = -origin.z() / dir.z();
            if (t > 0) {
                QVector3D gp = origin + dir * t;
                gx = floor(gp.x() + gridSize/2.0f);
                gy = floor(gp.y() + gridSize/2.0f);
                if (gx >= 0 && gx < gridSize && gy >= 0 && gy < gridSize) hitGround = true;
            }
        }

        if (!(hitVoxel || hitGround)) return;

        if (event->button() == Qt::LeftButton) {
            if (hitVoxel && hitFace >= 0) {
                int idx = voxelIndexFromWorld(hitBlock);
                if (idx >= 0 && voxelGrid[idx].faces[hitFace].interactive) {
                    triggerInteraction(idx, hitFace);
                    return;
                }
            }
            int ax, ay, az;
            if (hitVoxel) {
                ax = adjacentBlock.x() + gridSize/2.0f;
                ay = adjacentBlock.y() + gridSize/2.0f;
                az = adjacentBlock.z();
            } else { ax = gx; ay = gy; az = 0; }

            if (ax >= 0 && ax < gridSize && ay >= 0 && ay < gridSize && az >= 0 && az < gridSize) {
                int idx = index(ax, ay, az);
                if (!voxelGrid[idx].active) {
                    voxelGrid[idx].active = true;
                    for (int f = 0; f < 6; f++) voxelGrid[idx].faces[f].texId = currentTextureIdx;
                    voxelGrid[idx].type = currentTextureIdx;
                    voxelGrid[idx].age = 0;
                    update();
                }
            }
        } else if (event->button() == Qt::RightButton) {
            if (hitVoxel) removeVoxelAt(hitBlock);
        } else if (event->button() == Qt::MiddleButton) {
            if (hitVoxel && hitFace >= 0) {
                int idx = voxelIndexFromWorld(hitBlock);
                if (idx >= 0) {
                    currentTextureIdx = voxelGrid[idx].faces[hitFace].texId;
                    updateInfoLabel();
                    update();
                }
            }
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (firstPerson && event->button() == Qt::RightButton && !rightDragged) {
            QVector3D origin, dir;
            getViewRay(QPointF(width()/2.0, height()/2.0), origin, dir);
            QVector3D hb, ab; int t, f;
            if (raycastVoxel(origin, dir, hb, ab, t, f)) removeVoxelAt(hb);
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
            if (event->buttons() & Qt::RightButton) {
                if ((event->pos() - rightPressPos).manhattanLength() > 4) rightDragged = true;
                fpYaw += dx * 0.3f;
                fpPitch -= dy * 0.3f;
                if (fpPitch > 89.0f) fpPitch = 89.0f;
                if (fpPitch < -89.0f) fpPitch = -89.0f;
                update();
            }
        } else if (event->buttons() & Qt::MiddleButton) {
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

        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6) {
            currentTextureIdx = event->key() - Qt::Key_1;
            updateInfoLabel();
        }
        if (event->key() == Qt::Key_F) {
            firstPerson = !firstPerson;
            if (firstPerson) { fpYaw = yaw + 180.0f; fpPitch = 0.0f; }
            updateInfoLabel();
        }
        if (event->key() == Qt::Key_E) assignInteractionToFace();
        update();
    }

    void keyReleaseEvent(QKeyEvent *event) override {
        pressedKeys.remove(event->key());
    }

private:
    // ---------- TEXTURES ----------
    int getTextureForPath(const QString& path) {
        if (customTextureCache.contains(path)) return customTextureCache[path];
        QImage img(path);
        if (img.isNull()) { qWarning() << "Could not load image:" << path; return 5; }

        // Scale to power-of-two + flip vertically for OpenGL
        QImage gl = img.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .mirrored(false, true)
                       .convertToFormat(QImage::Format_RGBA8888);

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gl.width(), gl.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, gl.bits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        int idx = (int)textures.size();
        textures.push_back(tex);
        customTextureCache[path] = idx;
        qDebug() << "Loaded poster texture:" << path << "as index" << idx;
        return idx;
    }

    GLuint createTexture(QColor baseColor, const QString& name) {
        QImage img(64, 64, QImage::Format_RGBA8888);
        img.fill(baseColor);
        for(int i=0; i<64; ++i) for(int j=0; j<64; ++j) {
            QColor c = baseColor;
            if (name == "Brick") {
                if (i % 16 == 0 || j % 32 == 0 || (j % 32 == 16 && i % 32 > 15)) c = QColor(200,200,200);
            } else if (name == "Poster") {
                if (i < 4 || i > 59 || j < 4 || j > 59) c = QColor(20,20,80);
                else if (i > 20 && i < 44 && j > 10 && j < 54) c = QColor(255,255,200);
                else c = QColor(40,40,120);
            } else {
                int noise = (qrand() % 40) - 20;
                c = QColor(qBound(0, c.red()+noise, 255), qBound(0, c.green()+noise, 255), qBound(0, c.blue()+noise, 255));
                if (i==0 || i==63 || j==0 || j==63) c = c.darker(150);
            }
            img.setPixelColor(i, j, c);
        }
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return tex;
    }

    // ---------- FACE ASSIGNMENT ----------
    void assignInteractionToFace() {
        QVector3D origin, dir;
        getViewRay(firstPerson ? QPointF(width()/2.0, height()/2.0) : QPointF(lastMousePosition), origin, dir);

        QVector3D hb, ab; int t, hitFace;
        if (!raycastVoxel(origin, dir, hb, ab, t, hitFace)) return;
        int idx = voxelIndexFromWorld(hb);
        if (idx < 0) return;

        bool ok = false;
        QStringList options;
        options << "Movie poster (image + URL)" << "Image texture only" << "URL link only" << "Clear assignment";
        QString choice = QInputDialog::getItem(this, "Assign to Face", "What should this face do?", options, 0, false, &ok);
        if (!ok) { pressedKeys.clear(); return; }

        FaceData& fd = voxelGrid[idx].faces[hitFace];
        makeCurrent();

        if (choice == options[0] || choice == options[1]) {
            QString imgPath = QFileDialog::getOpenFileName(this, "Choose Poster Image", "",
                "Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)");
            if (!imgPath.isEmpty()) {
                fd.imagePath = imgPath;
                fd.texId = getTextureForPath(imgPath);
            } else if (choice == options[0]) {
                fd.texId = 5; // fallback procedural poster
            }
        }
        if (choice == options[0] || choice == options[2]) {
            if (choice == options[2]) fd.texId = 5;
            bool ok2 = false;
            QString url = QInputDialog::getText(this, "Assign URL / Movie",
                "URL or local file to open when triggered:", QLineEdit::Normal, "https://", &ok2);
            if (ok2 && !url.isEmpty()) {
                fd.interactive = true;
                fd.interactionType = 1;
                fd.url = url;
            }
        }
        if (choice == options[3]) {
            fd = FaceData();
            fd.texId = currentTextureIdx;
        }

        doneCurrent();
        pressedKeys.clear(); // avoid stuck keys after modal dialogs
        updateInfoLabel();
        update();
    }

    TargetInfo getTarget(const QPointF& screenPos) {
        TargetInfo t;
        QVector3D origin, dir;
        getViewRay(screenPos, origin, dir);
        if (raycastVoxel(origin, dir, t.hitBlock, t.adjacentBlock, t.texId, t.face)) {
            t.valid = true;
            t.voxelIdx = voxelIndexFromWorld(t.hitBlock);
        }
        return t;
    }

    void drawCrosshair() {
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, width(), height(), 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST);

        float cx = width() / 2.0f, cy = height() / 2.0f, s = 9.0f;
        glLineWidth(2.0f);
        glColor4f(1.0f, 1.0f, 1.0f, 0.9f);
        glBegin(GL_LINES);
        glVertex2f(cx - s, cy); glVertex2f(cx + s, cy);
        glVertex2f(cx, cy - s); glVertex2f(cx, cy + s);
        glEnd();

        glEnable(GL_DEPTH_TEST);
        glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
    }

    void drawTargetHighlight(const TargetInfo& t) {
        // --- Yellow wireframe around the targeted block ---
        glPushMatrix();
        glTranslatef(t.hitBlock.x(), t.hitBlock.y(), t.hitBlock.z());
        glDisable(GL_TEXTURE_2D);
        float e = 0.505f;
        glLineWidth(2.5f);
        glColor4f(1.0f, 0.9f, 0.2f, 1.0f);
        glBegin(GL_LINES);
        glVertex3f(-e,-e,-e); glVertex3f( e,-e,-e);  glVertex3f( e,-e,-e); glVertex3f( e, e,-e);
        glVertex3f( e, e,-e); glVertex3f(-e, e,-e);  glVertex3f(-e, e,-e); glVertex3f(-e,-e,-e);
        glVertex3f(-e,-e, e); glVertex3f( e,-e, e);  glVertex3f( e,-e, e); glVertex3f( e, e, e);
        glVertex3f( e, e, e); glVertex3f(-e, e, e);  glVertex3f(-e, e, e); glVertex3f(-e,-e, e);
        glVertex3f(-e,-e,-e); glVertex3f(-e,-e, e);  glVertex3f( e,-e,-e); glVertex3f( e,-e, e);
        glVertex3f( e, e,-e); glVertex3f( e, e, e);  glVertex3f(-e, e,-e); glVertex3f(-e, e, e);
        glEnd();

        // --- Blue tint on the exact face being picked ---
        glColor4f(0.3f, 0.7f, 1.0f, 0.45f);
        drawFace(t.face);
        glPopMatrix();

        // --- Ghost preview of where the new block will go ---
        glPushMatrix();
        glTranslatef(t.adjacentBlock.x(), t.adjacentBlock.y(), t.adjacentBlock.z());
        glColor4f(1.0f, 1.0f, 1.0f, 0.18f);
        float g = 0.5f;
        glBegin(GL_QUADS);
        glVertex3f(-g,-g, g); glVertex3f( g,-g, g); glVertex3f( g, g, g); glVertex3f(-g, g, g);
        glVertex3f(-g,-g,-g); glVertex3f( g,-g,-g); glVertex3f( g, g,-g); glVertex3f(-g, g,-g);
        glVertex3f(-g,-g,-g); glVertex3f(-g,-g, g); glVertex3f(-g, g, g); glVertex3f(-g, g,-g);
        glVertex3f( g,-g,-g); glVertex3f( g,-g, g); glVertex3f( g, g, g); glVertex3f( g, g,-g);
        glVertex3f(-g, g,-g); glVertex3f( g, g,-g); glVertex3f( g, g, g); glVertex3f(-g, g, g);
        glVertex3f(-g,-g,-g); glVertex3f( g,-g,-g); glVertex3f( g,-g, g); glVertex3f(-g,-g, g);
        glEnd();
        glPopMatrix();
    }

    void triggerInteraction(int voxelIdx, int face) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastInteractionTime < 2000) return; // cooldown
        lastInteractionTime = now;
        FaceData& fd = voxelGrid[voxelIdx].faces[face];
        if (fd.interactive && fd.interactionType == 1) {
            QUrl u = fd.url.startsWith("http", Qt::CaseInsensitive) ? QUrl(fd.url) : QUrl::fromLocalFile(fd.url);
            qDebug().noquote() << ">>> OPENING POSTER:" << u.toString();
            QDesktopServices::openUrl(u);
        }
    }

    // ---------- HELPERS ----------
    int voxelIndexFromWorld(const QVector3D& wb) {
        int x = wb.x() + gridSize/2.0f, y = wb.y() + gridSize/2.0f, z = wb.z();
        if (x < 0 || x >= gridSize || y < 0 || y >= gridSize || z < 0 || z >= gridSize) return -1;
        return index(x, y, z);
    }

    void removeVoxelAt(const QVector3D& wb) {
        int idx = voxelIndexFromWorld(wb);
        if (idx >= 0) { voxelGrid[idx].active = false; update(); }
    }

    void getViewRay(const QPointF& mousePos, QVector3D& origin, QVector3D& dir) {
        if (firstPerson) {
            origin = QVector3D(player.pos.x(), player.pos.y(), player.pos.z() + 1.2f);
            dir = QVector3D(cos(fpPitch*M_PI/180.0f)*cos(fpYaw*M_PI/180.0f),
                            cos(fpPitch*M_PI/180.0f)*sin(fpYaw*M_PI/180.0f),
                            sin(fpPitch*M_PI/180.0f)).normalized();
            return;
        }
        float eyeX = target.x() + distance * cos(pitch*M_PI/180.0f) * cos(yaw*M_PI/180.0f);
        float eyeY = target.y() + distance * cos(pitch*M_PI/180.0f) * sin(yaw*M_PI/180.0f);
        float eyeZ = target.z() + distance * sin(pitch*M_PI/180.0f);
        origin = QVector3D(eyeX, eyeY, eyeZ);

        QVector3D forward = (target - origin).normalized();
        QVector3D right = QVector3D::crossProduct(forward, QVector3D(0,0,1)).normalized();
        QVector3D up = QVector3D::crossProduct(right, forward).normalized();

        float w = width(); if (w <= 0) w = 800;
        float h = height(); if (h <= 0) h = 600;
        float ndcX = (2.0f * mousePos.x() / w) - 1.0f;
        float ndcY = 1.0f - (2.0f * mousePos.y() / h);
        float tanFov = tan(60.0f * 0.5f * M_PI / 180.0f);

        dir = (right * (ndcX * (w/h) * tanFov) + up * (ndcY * tanFov) + forward).normalized();
    }

    bool raycastVoxel(QVector3D origin, QVector3D dir, QVector3D &hitBlockWorld,
                      QVector3D &adjacentWorld, int &hitTexId, int &hitFace) {
        QVector3D g(origin.x() + gridSize/2.0f, origin.y() + gridSize/2.0f, origin.z());
        g += dir * 0.0001f;

        int x = floor(g.x()), y = floor(g.y()), z = floor(g.z());
        int stepX = (dir.x() > 0) ? 1 : ((dir.x() < 0) ? -1 : 0);
        int stepY = (dir.y() > 0) ? 1 : ((dir.y() < 0) ? -1 : 0);
        int stepZ = (dir.z() > 0) ? 1 : ((dir.z() < 0) ? -1 : 0);

        float tDX = stepX ? std::abs(1.0f/dir.x()) : 1e30f;
        float tDY = stepY ? std::abs(1.0f/dir.y()) : 1e30f;
        float tDZ = stepZ ? std::abs(1.0f/dir.z()) : 1e30f;

        float tMX = stepX > 0 ? (x+1.0f-g.x())*tDX : stepX < 0 ? (g.x()-x)*tDX : 1e30f;
        float tMY = stepY > 0 ? (y+1.0f-g.y())*tDY : stepY < 0 ? (g.y()-y)*tDY : 1e30f;
        float tMZ = stepZ > 0 ? (z+1.0f-g.z())*tDZ : stepZ < 0 ? (g.z()-z)*tDZ : 1e30f;

        int px = x, py = y, pz = z, lastAxis = -1, lastDir = 0;

        for (int i = 0; i < 300; ++i) {
            if (x < -5 || x >= gridSize+5 || y < -5 || y >= gridSize+5 || z < -5 || z >= gridSize+5) break;
            if (x >= 0 && x < gridSize && y >= 0 && y < gridSize && z >= 0 && z < gridSize) {
                int idx = index(x, y, z);
                if (voxelGrid[idx].active) {
                    hitBlockWorld = QVector3D(x - gridSize/2.0f, y - gridSize/2.0f, z);
                    adjacentWorld = QVector3D(px - gridSize/2.0f, py - gridSize/2.0f, pz);
                    hitTexId = voxelGrid[idx].faces[0].texId;
                    if (lastAxis == 0) hitFace = lastDir > 0 ? FACE_X_NEG : FACE_X_POS;
                    else if (lastAxis == 1) hitFace = lastDir > 0 ? FACE_Y_NEG : FACE_Y_POS;
                    else if (lastAxis == 2) hitFace = lastDir > 0 ? FACE_Z_NEG : FACE_Z_POS;
                    else hitFace = 0;
                    return true;
                }
            }
            px = x; py = y; pz = z;
            if (tMX < tMY) {
                if (tMX < tMZ) { x += stepX; tMX += tDX; lastAxis = 0; lastDir = stepX; }
                else           { z += stepZ; tMZ += tDZ; lastAxis = 2; lastDir = stepZ; }
            } else {
                if (tMY < tMZ) { y += stepY; tMY += tDY; lastAxis = 1; lastDir = stepY; }
                else           { z += stepZ; tMZ += tDZ; lastAxis = 2; lastDir = stepZ; }
            }
        }
        return false;
    }

    void updatePhysics() {
        QTime now = QTime::currentTime();
        float dt = lastTime.msecsTo(now) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        lastTime = now;

        QVector3D fwd, rgt;
        if (firstPerson) {
            fwd = QVector3D(cos(fpYaw*M_PI/180.0f), sin(fpYaw*M_PI/180.0f), 0);
            rgt = QVector3D(-sin(fpYaw*M_PI/180.0f), cos(fpYaw*M_PI/180.0f), 0);
        } else {
            QVector3D eye; getViewRay(QPointF(width()/2.0, height()/2.0), eye, fwd);
            fwd = target - eye; fwd.setZ(0);
            if (fwd.length() > 0.001f) fwd.normalize();
            rgt = QVector3D::crossProduct(fwd, QVector3D(0,0,1)).normalized();
        }

        QVector3D moveDir(0,0,0);
        if (pressedKeys.contains(Qt::Key_W)) moveDir += fwd;
        if (pressedKeys.contains(Qt::Key_S)) moveDir -= fwd;
        if (pressedKeys.contains(Qt::Key_D)) moveDir -= rgt;
        if (pressedKeys.contains(Qt::Key_A)) moveDir += rgt;

        if (moveDir.length() > 0) {
            moveDir.normalize();
            player.vel.setX(player.vel.x() + moveDir.x() * 25.0f * dt);
            player.vel.setY(player.vel.y() + moveDir.y() * 25.0f * dt);
        }
        player.vel.setZ(player.vel.z() - 25.0f * dt);
        float damp = 1.0f - 10.0f * dt; if (damp < 0) damp = 0;
        player.vel.setX(player.vel.x() * damp);
        player.vel.setY(player.vel.y() * damp);

        QVector3D np = player.pos + player.vel * dt;
        float r = 0.4f;
        bool onGround = false;
        bool touchedWall = false;
        int wallIdx = -1, wallFace = -1, wallX = 0, wallY = 0, wallZ = 0;

        int minX = floor(np.x() + gridSize/2.0f - r), maxX = ceil(np.x() + gridSize/2.0f + r);
        int minY = floor(np.y() + gridSize/2.0f - r), maxY = ceil(np.y() + gridSize/2.0f + r);
        int minZ = floor(np.z() - r), maxZ = ceil(np.z() + r);

        for(int bx = minX; bx <= maxX; ++bx)
        for(int by = minY; by <= maxY; ++by)
        for(int bz = minZ; bz <= maxZ; ++bz) {
            if (bx<0 || bx>=gridSize || by<0 || by>=gridSize || bz<0 || bz>=gridSize) continue;
            int idx = index(bx,by,bz);
            if (!voxelGrid[idx].active) continue;

            float vMinX = bx - gridSize/2.0f, vMaxX = vMinX + 1.0f;
            float vMinY = by - gridSize/2.0f, vMaxY = vMinY + 1.0f;
            float vMinZ = bz, vMaxZ = bz + 1.0f;

            if (np.x()+r > vMinX && np.x()-r < vMaxX &&
                np.y()+r > vMinY && np.y()-r < vMaxY &&
                np.z()+r > vMinZ && np.z()-r < vMaxZ) {

                QVector3D c(vMinX+0.5f, vMinY+0.5f, vMinZ+0.5f);
                QVector3D diff = np - c;

                if (std::abs(diff.z()) > std::abs(diff.x()) && std::abs(diff.z()) > std::abs(diff.y())) {
                    if (diff.z() > 0) { np.setZ(vMaxZ + r); player.vel.setZ(0); onGround = true; }
                    else              { np.setZ(vMinZ - r); player.vel.setZ(0); }
                } else {
                    int hitFace;
                    if (std::abs(diff.x()) > std::abs(diff.y())) {
                        hitFace = diff.x() > 0 ? FACE_X_NEG : FACE_X_POS;
                        np.setX(diff.x() > 0 ? vMaxX + r : vMinX - r);
                        player.vel.setX(0);
                    } else {
                        hitFace = diff.y() > 0 ? FACE_Y_NEG : FACE_Y_POS;
                        np.setY(diff.y() > 0 ? vMaxY + r : vMinY - r);
                        player.vel.setY(0);
                    }
                    touchedWall = true;
                    wallIdx = idx; wallFace = hitFace;
                    wallX = bx; wallY = by; wallZ = bz;
                }
            }
        }

        if (pressedKeys.contains(Qt::Key_Space) && onGround) player.vel.setZ(12.0f);
        // --- Collision logging: only prints when the contacted block/face CHANGES ---
        if (touchedWall) {
            if (wallIdx != lastCollideIdx || wallFace != lastCollideFace) {
                logCollision(wallIdx, wallFace, wallX, wallY, wallZ);
                lastCollideIdx = wallIdx;
                lastCollideFace = wallFace;
            }
            if (voxelGrid[wallIdx].faces[wallFace].interactive) {
                triggerInteraction(wallIdx, wallFace);
            }
        } else {
            lastCollideIdx = -1;
            lastCollideFace = -1;
        }
        if (np.z() < -10.0f) { np.setZ(3.0f); player.vel.setZ(0); }
        player.pos = np;
    }

    void updateInfoLabel() {
        if (!infoLabel) return;
        QStringList names = {"Dirt", "Stone", "Grass", "Brick", "Sand", "Poster"};
        QString sel = currentTextureIdx < names.size() ? names[currentTextureIdx] : "Custom";
        infoLabel->setText(QString("[%1] Tex: %2 | F: View | E: Assign Face | Ctrl+S/O: Save/Load | WASD+Space | 1-6: Tex")
            .arg(firstPerson ? "FIRST PERSON" : "ORBIT").arg(sel));
    }

    void drawFace(int face) {
        glBegin(GL_QUADS);
        switch(face) {
        case FACE_X_NEG:
            glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(-0.5,-0.5,0.5);
            glTexCoord2f(1,1); glVertex3f(-0.5,0.5,0.5);  glTexCoord2f(0,1); glVertex3f(-0.5,0.5,-0.5); break;
        case FACE_X_POS:
            glTexCoord2f(0,0); glVertex3f(0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,0.5);
            glTexCoord2f(1,1); glVertex3f(0.5,0.5,0.5);   glTexCoord2f(0,1); glVertex3f(0.5,0.5,-0.5); break;
        case FACE_Y_NEG:
            glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,-0.5);
            glTexCoord2f(1,1); glVertex3f(0.5,0.5,-0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,0.5,-0.5); break;
        case FACE_Y_POS:
            glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,0.5);
            glTexCoord2f(1,1); glVertex3f(0.5,0.5,0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,0.5,0.5); break;
        case FACE_Z_NEG:
            glTexCoord2f(0,0); glVertex3f(-0.5,-0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,-0.5,-0.5);
            glTexCoord2f(1,1); glVertex3f(0.5,-0.5,0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,-0.5,0.5); break;
        case FACE_Z_POS:
            glTexCoord2f(0,0); glVertex3f(-0.5,0.5,-0.5); glTexCoord2f(1,0); glVertex3f(0.5,0.5,-0.5);
            glTexCoord2f(1,1); glVertex3f(0.5,0.5,0.5);   glTexCoord2f(0,1); glVertex3f(-0.5,0.5,0.5); break;
        }
        glEnd();
    }

    QString faceName(int f) const {
        switch(f) {
            case FACE_X_NEG: return "LEFT(-X)";
            case FACE_X_POS: return "RIGHT(+X)";
            case FACE_Y_NEG: return "BACK(-Y)";
            case FACE_Y_POS: return "FRONT(+Y)";
            case FACE_Z_NEG: return "BOTTOM(-Z)";
            case FACE_Z_POS: return "TOP(+Z)";
        }
        return "?";
    }

    void logCollision(int idx, int face, int bx, int by, int bz) {
        const FaceData& fd = voxelGrid[idx].faces[face];
        QString msg = QString("BUMP  voxel(%1,%2,%3) face=%4 tex=%5")
            .arg(bx).arg(by).arg(bz)
            .arg(faceName(face)).arg(fd.texId);
        if (fd.interactive)        msg += QString("  [POSTER] url=%1").arg(fd.url);
        if (!fd.imagePath.isEmpty()) msg += QString("  img=%1").arg(fd.imagePath);
        qDebug().noquote() << msg;
    }

    void drawVoxel(int x, int y, int z) {
        int idx = index(x, y, z);
        glPushMatrix();
        glTranslatef(x - gridSize/2.0f, y - gridSize/2.0f, z);
        glEnable(GL_TEXTURE_2D);
        for (int f = 0; f < 6; f++) {
            const FaceData& fd = voxelGrid[idx].faces[f];
            int t = fd.texId;
            if (t < 0 || t >= (int)textures.size()) t = 0;
            glBindTexture(GL_TEXTURE_2D, textures[t]);
            glColor3f(1.0f, 1.0f, 1.0f);
            drawFace(f);
            if (fd.interactive) { // highlight border for interactive faces
                glDisable(GL_TEXTURE_2D);
                glColor4f(0.2f, 0.6f, 1.0f, 0.35f);
                drawFace(f);
                glEnable(GL_TEXTURE_2D);
            }
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
    float distance, yaw, pitch, fpYaw, fpPitch;
    bool firstPerson;
    int currentTextureIdx;
    QVector3D target;
    QSet<int> pressedKeys;
    QPoint lastMousePosition, rightPressPos;
    bool rightDragged = false;
    std::vector<Voxel> voxelGrid;
    std::vector<GLuint> textures;
    QMap<QString, int> customTextureCache;
    Entity player;
    QTime lastTime;
    QLabel* infoLabel = nullptr;
    qint64 lastInteractionTime;
    int lastCollideIdx = -1;
    int lastCollideFace = -1;
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

    QLabel* lbl = new QLabel("[ORBIT] F: View | E: Assign Face | Ctrl+S/O: Save/Load | WASD+Space | 1-6: Tex");
    lbl->setStyleSheet("color: yellow; background: rgba(0,0,0,180); padding: 5px; font-weight: bold;");
    layout->addWidget(lbl);
    glWidget->setInfoLabel(lbl);

    w.setCentralWidget(central);

    QMenu* fileMenu = w.menuBar()->addMenu("&File");
    QAction* saveAct = fileMenu->addAction("Save World");
    saveAct->setShortcut(QKeySequence("Ctrl+S"));
    QObject::connect(saveAct, &QAction::triggered, glWidget, [glWidget]() { glWidget->saveWorld(); });
    QAction* loadAct = fileMenu->addAction("Load World");
    loadAct->setShortcut(QKeySequence("Ctrl+O"));
    QObject::connect(loadAct, &QAction::triggered, glWidget, [glWidget]() { glWidget->loadWorld(); });

    w.resize(900, 700);
    w.show();
    return a.exec();
}

#include "main.moc"
