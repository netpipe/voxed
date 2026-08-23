#include <irrlicht.h>
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QTimer>
#include <QMouseEvent>
#include <iostream>
#include <unordered_map>
#include "VoxelNode.h"

using namespace irr;

class VoxelEditor : public QWidget, public IEventReceiver {
    Q_OBJECT

public:
    VoxelEditor();
    ~VoxelEditor();
    void run();

    // Override the OnEvent function to handle events
    virtual bool OnEvent(const SEvent &event);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onSelectTexture();
    void onUpdate();

private:
    void createScene();
    void placeVoxel(const core::vector3df &position);
    void removeVoxel(scene::ISceneNode *node);
    void scaleVoxel(VoxelNode* voxelNode, float scale);

    IrrlichtDevice *mDevice;
    video::IVideoDriver *mDriver;
    scene::ISceneManager *mSceneMgr;
    scene::ICameraSceneNode *mCamera;
    bool mRunning;
    std::string mCurrentTexture;
    scene::ISceneCollisionManager* cm;

    QTimer *mTimer;
    u32 mLastClickTime;
    core::vector3df mLastClickPos;
    std::unordered_map<scene::ISceneNode *, VoxelNode *> mVoxelMap;

    bool mLeftMousePressed;
    bool mRightMousePressed;
};

VoxelEditor::VoxelEditor()
    : mDevice(nullptr),
      mDriver(nullptr),
      mSceneMgr(nullptr),
      mCamera(nullptr),
      mRunning(true),
      mLastClickTime(0),
      mLeftMousePressed(false),
      mRightMousePressed(false) {
    mCurrentTexture = "default.png";

    QVBoxLayout *layout = new QVBoxLayout(this);
    QPushButton *textureButton = new QPushButton("Select Texture", this);
    layout->addWidget(textureButton);

    connect(textureButton, &QPushButton::clicked, this, &VoxelEditor::onSelectTexture);

    mTimer = new QTimer(this);
    connect(mTimer, &QTimer::timeout, this, &VoxelEditor::onUpdate);
    mTimer->start(16);  // roughly 60 FPS

    mDevice = createDevice(video::EDT_OPENGL, core::dimension2d<u32>(800, 600), 16, false, false, false, this);
    if (!mDevice) {
        throw std::runtime_error("Failed to initialize Irrlicht");
    }

    mDriver = mDevice->getVideoDriver();
    mSceneMgr = mDevice->getSceneManager();
    mCamera = mSceneMgr->addCameraSceneNode();
    mCamera->setPosition(core::vector3df(0, 30, -40));
    mCamera->setTarget(core::vector3df(0, 0, 0));

    createScene();
}

VoxelEditor::~VoxelEditor() {
    if (mDevice) {
        mDevice->drop();
    }
}

void VoxelEditor::run() {
    mRunning = true;
    while (mDevice->run() && mRunning) {
        mDriver->beginScene(true, true, video::SColor(255, 100, 101, 140));
        mSceneMgr->drawAll();
        mDriver->endScene();
    }
}

void VoxelEditor::createScene() {
    // Initialize the scene (currently empty as we dynamically place voxels)
}

bool VoxelEditor::OnEvent(const SEvent &event) {
    if (event.EventType == EET_MOUSE_INPUT_EVENT) {
        switch (event.MouseInput.Event) {
        case EMIE_LMOUSE_PRESSED_DOWN:
            mLeftMousePressed = true;
            break;
        case EMIE_LMOUSE_LEFT_UP:
            mLeftMousePressed = false;
            break;
        case EMIE_RMOUSE_PRESSED_DOWN:
            mRightMousePressed = true;
            break;
        case EMIE_RMOUSE_LEFT_UP:
            mRightMousePressed = false;
            break;
        case EMIE_MOUSE_MOVED:
            if (mLeftMousePressed || mRightMousePressed) {
                core::position2di cursorPos = mDevice->getCursorControl()->getPosition();
                core::line3df ray = cm->getRayFromScreenCoordinates(cursorPos, mCamera);
                core::vector3df intersection;
                core::triangle3df hitTriangle;
                scene::ISceneNode *selectedNode = cm->getSceneNodeAndCollisionPointFromRay(ray, intersection, hitTriangle);

                if (selectedNode) {
                    if (mRightMousePressed) {
                        // Remove voxel on right-click
                        removeVoxel(selectedNode);
                    } else if (mLeftMousePressed) {
                        // Place voxel on left-click
                        placeVoxel(intersection);
                    }
                }
            }
            break;
        default:
            break;
        }
    }
    return false;
}

void VoxelEditor::placeVoxel(const core::vector3df &position) {
    scene::IMeshSceneNode *voxel = mSceneMgr->addCubeSceneNode(1.0f, 0, -1, position);
    voxel->setMaterialFlag(video::EMF_LIGHTING, false);
    voxel->setMaterialTexture(0, mDriver->getTexture(mCurrentTexture.c_str()));
    VoxelNode* voxelNode = new VoxelNode(voxel, 0, 1.0f);
    mVoxelMap[voxel] = voxelNode;
}

void VoxelEditor::removeVoxel(scene::ISceneNode *node) {
    auto it = mVoxelMap.find(node);
    if (it != mVoxelMap.end()) {
        node->remove();
        delete it->second; // Ensure to delete the VoxelNode pointer
        mVoxelMap.erase(it);
    }
}

void VoxelEditor::scaleVoxel(VoxelNode* voxelNode, float scale) {
    if (voxelNode) {
        voxelNode->getNode()->setScale(core::vector3df(scale, scale, scale));
        voxelNode->setLastSize(scale);
    }
}

void VoxelEditor::onSelectTexture() {
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select Texture"), "", tr("Images (*.png *.jpg *.bmp)"));
    if (!filePath.isEmpty()) {
        mCurrentTexture = filePath.toStdString();
    }
}

void VoxelEditor::onUpdate() {
    if (mDevice) {
        mDevice->run();
        mDriver->beginScene(true, true, video::SColor(255, 100, 101, 140));
        mSceneMgr->drawAll();
        mDriver->endScene();
    }
}

void VoxelEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mLeftMousePressed = true;
    } else if (event->button() == Qt::RightButton) {
        mRightMousePressed = true;
    }
}

void VoxelEditor::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mLeftMousePressed = false;
    } else if (event->button() == Qt::RightButton) {
        mRightMousePressed = false;
    }
}

void VoxelEditor::mouseMoveEvent(QMouseEvent* event) {
    core::position2di cursorPos(event->pos().x(), event->pos().y());
    core::line3df ray = cm->getRayFromScreenCoordinates(cursorPos, mCamera);
    core::vector3df intersection;
    core::triangle3df hitTriangle;
    scene::ISceneNode *selectedNode = cm->getSceneNodeAndCollisionPointFromRay(ray, intersection, hitTriangle);

    if (selectedNode) {
        if (mRightMousePressed) {
            removeVoxel(selectedNode);
        } else if (mLeftMousePressed) {
            placeVoxel(intersection);
        }
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    VoxelEditor editor;
    editor.show();

    return app.exec();
}

#include "main.moc"
