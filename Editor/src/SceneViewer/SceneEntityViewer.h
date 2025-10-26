//
// Created by Administrator on 2025/8/25.
//

#ifndef SCENEENTITYVIEWER_H
#define SCENEENTITYVIEWER_H
#include "../thirdparty/thirdparty.h"

namespace MQEngine {

    class SceneEntityViewer {
    public:
        SceneEntityViewer();
        void renderGlobalEntityList(Scene* scene);
        void renderTrunkEntityList(Scene* scene, std::string trunkName);
        void renderSceneEntityList(Scene* scene);
        void render();
        void selectEntity(entt::entity entity, const std::string& trunkName = "", bool isGlobal = false);
    private:
        DataManager* m_dataManager;

        bool m_showCreateEntityDialog = false;
        char m_newEntityName[256] = "entity";
        std::string m_targetTrunkName;
        bool m_createInGlobal = true;

        bool m_showContextMenu = false;
        std::string m_contextMenuTarget;

        void renderCreateEntityDialog(Scene* scene);
        void showCreateEntityDialog(const std::string& targetTrunk = "", bool isGlobal = true);
        void createEntity(Scene* scene, const std::string& name, const std::string& trunkName = "", bool isGlobal = true);
        void deleteEntity(entt::entity entity, const std::string& trunkName = "", bool isGlobal = false);
        void addStaticMeshComponent(entt::entity entity, const std::string& modelUuid, const std::string& meshName, bool isGlobal, const std::string& trunkName = "");
        void addScriptComponent(entt::entity entity, const std::string& functionName, bool isGlobal, const std::string& trunkName = "");
        void addDiffuseTextureComponent(entt::entity entity, const std::string& modelUuid, const std::string& texturePath, bool isGlobal, const std::string& trunkName = "");
        void addNormalTextureComponent(entt::entity entity, const std::string& modelUuid, const std::string& texturePath, bool isGlobal, const std::string& trunkName = "");
        void addAlbedoTextureComponent(entt::entity entity, const std::string& modelUuid, const std::string& texturePath, bool isGlobal, const std::string& trunkName);

        void renderScriptTypeSelectionPopup();
        void renderTextureTypeSelectionPopup();
        void openTextureTypePopup(entt::entity entity, const std::string& modelUuid, const std::string& texturePath, bool isGlobal, const std::string& trunkName);

        bool m_showScriptTypePopup = false;
        std::string m_draggedFunctionName;
        entt::entity m_targetEntity;
        bool m_targetIsGlobal;

        bool m_showTextureTypePopup = false;
        std::string m_draggedTexturePath;
        std::string m_draggedModelUuid;
    };

} // MQEngine

#endif //SCENEENTITYVIEWER_H
