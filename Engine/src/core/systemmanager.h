/**
 *@file SystemManager.h
 *@brief class header for start and stop system
 */
#ifndef SYSTEMMANAGER_H
#define SYSTEMMANAGER_H
namespace MQEngine {
    class SystemManager {
    public:
        SystemManager()
        {
            FCT::fout << "SystemManager::SystemManager()\n";
        }
        void init();
        void term();
    private:
    };

}
#endif //SYSTEMMANAGER_H
