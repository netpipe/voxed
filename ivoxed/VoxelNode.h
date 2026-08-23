#ifndef VOXELNODE_H
#define VOXELNODE_H

#include <irrlicht.h>

using namespace irr;

class VoxelNode {
public:
    VoxelNode(scene::IMeshSceneNode* node, int type, float weight);

    scene::IMeshSceneNode* getNode() const;
    void setNode(scene::IMeshSceneNode* node);

    int getAge() const;
    void setAge(int age);

    int getType() const;
    void setType(int type);

    float getLastSize() const;
    void setLastSize(float size);

    float getWeight() const;
    void setWeight(float weight);

    float getAffectorValue() const;
    void setAffectorValue(float value);

private:
    scene::IMeshSceneNode* node;
    int age;
    int type;
    float lastSize;
    float weight;
    float affectorValue;
};

#endif // VOXELNODE_H

