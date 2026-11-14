#include "Common/E2ECommon.h"

using namespace MQEngine::Testing;

int main(int argc, char** argv) {
    return E2ETestRegistry::instance().run(TestTier::Tier1, argc, argv);
}

