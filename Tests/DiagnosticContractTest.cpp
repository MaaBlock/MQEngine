#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/manager/WorkHost.h>

using namespace MQEngine;
using namespace MQEngine::Host;

void myAsyncReadAndDispatch(
    WorkHost& workHost,
    std::string_view path,
    bcpp::work_contract contract,
    std::function<void(StatusOr<std::vector<uint8_t>>)> onReadComplete)
{
    struct BinaryReadContext {
        std::string filePath;
        StatusOr<std::vector<uint8_t>> result;
        std::function<void(StatusOr<std::vector<uint8_t>>)> completionCallback;
        bcpp::work_contract downstreamContract;
    };

    auto ctx = std::make_shared<BinaryReadContext>();
    ctx->filePath = std::string(path);
    ctx->completionCallback = std::move(onReadComplete);
    ctx->downstreamContract = std::move(contract);

    workHost.postIoTask([ctx]() {
        ctx->result = NotFoundError("ResourceLoader 不可用: " + ctx->filePath);
        if (ctx->completionCallback) {
            ctx->completionCallback(std::move(ctx->result));
        }
        std::cout << "[MY_IO_THREAD] ctx->downstreamContract.is_valid() = " << ctx->downstreamContract.is_valid() << std::endl;
        if (ctx->downstreamContract.is_valid()) {
            std::cout << "[MY_IO_THREAD] Scheduling downstreamContract..." << std::endl;
            ctx->downstreamContract.schedule();
        }
    });
}

int main()
{
    std::cout << "--- Diagnostic Contract Test ---" << std::endl;
    WorkHost workHost;
    workHost.init(2, 2);

    // Test A: Engine.dll asyncReadAndDispatch
    std::atomic<bool> executedA{false};
    auto contractA = workHost.createContract([&executedA]() {
        std::cout << "[WORKER THREAD] Contract A executed!" << std::endl;
        executedA.store(true);
    });
    workHost.asyncReadAndDispatch("test_A.txt", std::move(contractA), [](auto) {
        std::cout << "[IO THREAD A] onReadComplete A invoked." << std::endl;
    });

    // Test B: Local myAsyncReadAndDispatch
    std::atomic<bool> executedB{false};
    auto contractB = workHost.createContract([&executedB]() {
        std::cout << "[WORKER THREAD] Contract B executed!" << std::endl;
        executedB.store(true);
    });
    myAsyncReadAndDispatch(workHost, "test_B.txt", std::move(contractB), [](auto) {
        std::cout << "[IO THREAD B] onReadComplete B invoked." << std::endl;
    });

    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (executedA.load() && executedB.load()) break;
    }

    std::cout << "Result: executedA (Engine.dll) = " << executedA.load() << ", executedB (Local) = " << executedB.load() << std::endl;

    workHost.term();
    return 0;
}
