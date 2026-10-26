// SPDX-License-Identifier: AGPL-3.0-only
#include "LiteRtModel.h"

#include "litert/c/litert_compiled_model.h"
#include "litert/c/litert_environment.h"
#include "litert/c/litert_model.h"
#include "litert/c/litert_opaque_options.h"
#include "litert/c/litert_options.h"
#include "litert/c/litert_tensor_buffer.h"
#include "litert/c/options/litert_cpu_options.h"
#include "litert/c/options/litert_runtime_options.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace mimic
{
namespace
{
void check(LiteRtStatus status, const char* operation)
{
    if (status != kLiteRtStatusOk)
    {
        throw std::runtime_error(std::string("LiteRT ") + operation
                                 + " failed: " + std::to_string(status));
    }
}
} // namespace

struct LiteRtModel::Implementation
{
    std::size_t classes = 3;
    LiteRtEnvironment environment = nullptr;
    ::LiteRtModel model = nullptr;
    LiteRtOptions options = nullptr;
    LiteRtCompiledModel compiled = nullptr;
    LiteRtTensorBuffer input = nullptr;
    LiteRtTensorBuffer output = nullptr;

    ~Implementation()
    {
        if (output)
        {
            LiteRtDestroyTensorBuffer(output);
        }
        if (input)
        {
            LiteRtDestroyTensorBuffer(input);
        }
        if (compiled)
        {
            LiteRtDestroyCompiledModel(compiled);
        }
        if (options)
        {
            LiteRtDestroyOptions(options);
        }
        if (model)
        {
            LiteRtDestroyModel(model);
        }
        if (environment)
        {
            LiteRtDestroyEnvironment(environment);
        }
    }
};

LiteRtModel::LiteRtModel(const std::string& path)
    : implementation(std::make_unique<Implementation>())
{
    auto& state = *implementation;
    check(LiteRtCreateEnvironment(0, nullptr, &state.environment), "environment");
    check(LiteRtCreateModelFromFile(state.environment, path.c_str(), &state.model), "model load");
    check(LiteRtCreateOptions(&state.options), "options");
    check(LiteRtSetOptionsHardwareAccelerators(state.options, kLiteRtHwAcceleratorCpu),
          "CPU selection");
    LrtCpuOptions* cpu = nullptr;
    check(LrtCreateCpuOptions(&cpu), "CPU options");
    const auto kernelStatus = LrtSetCpuOptionsKernelMode(cpu, kLiteRtCpuKernelModeBuiltin);
    if (kernelStatus != kLiteRtStatusOk)
    {
        LrtDestroyCpuOptions(cpu);
        check(kernelStatus, "optimized CPU kernels");
    }
    const auto cpuStatus = LrtSetCpuOptionsNumThread(cpu, 1);
    if (cpuStatus != kLiteRtStatusOk)
    {
        LrtDestroyCpuOptions(cpu);
        check(cpuStatus, "CPU thread count");
    }
    const char* identifier = nullptr;
    void* payload = nullptr;
    void (*deleter)(void*) = nullptr;
    const auto dataStatus = LrtGetOpaqueCpuOptionsData(cpu, &identifier, &payload, &deleter);
    LrtDestroyCpuOptions(cpu);
    check(dataStatus, "CPU options payload");
    LiteRtOpaqueOptions opaque = nullptr;
    const auto opaqueStatus = LiteRtCreateOpaqueOptions(identifier, payload, deleter, &opaque);
    if (opaqueStatus != kLiteRtStatusOk)
    {
        if (deleter)
        {
            deleter(payload);
        }
        check(opaqueStatus, "opaque CPU options");
    }
    const auto addStatus = LiteRtAddOpaqueOptions(state.options, opaque);
    if (addStatus != kLiteRtStatusOk)
    {
        LiteRtDestroyOpaqueOptions(opaque);
        check(addStatus, "CPU configuration");
    }
    LrtRuntimeOptions* runtime = nullptr;
    check(LrtCreateRuntimeOptions(&runtime), "runtime options");
    const auto reporterStatus =
        LrtSetRuntimeOptionsErrorReporterMode(runtime, kLiteRtErrorReporterModeStderr);
    if (reporterStatus != kLiteRtStatusOk)
    {
        LrtDestroyRuntimeOptions(runtime);
        check(reporterStatus, "runtime error reporter");
    }
    const auto runtimeData =
        LrtGetOpaqueRuntimeOptionsData(runtime, &identifier, &payload, &deleter);
    LrtDestroyRuntimeOptions(runtime);
    check(runtimeData, "runtime payload");
    opaque = nullptr;
    const auto runtimeOpaque = LiteRtCreateOpaqueOptions(identifier, payload, deleter, &opaque);
    if (runtimeOpaque != kLiteRtStatusOk)
    {
        if (deleter)
        {
            deleter(payload);
        }
        check(runtimeOpaque, "runtime opaque options");
    }
    const auto runtimeAdd = LiteRtAddOpaqueOptions(state.options, opaque);
    if (runtimeAdd != kLiteRtStatusOk)
    {
        LiteRtDestroyOpaqueOptions(opaque);
        check(runtimeAdd, "runtime configuration");
    }
    check(LiteRtCreateCompiledModel(state.environment, state.model, state.options, &state.compiled),
          "compile");
    LiteRtParamIndex signatureCount = 0;
    check(LiteRtGetNumModelSignatures(state.model, &signatureCount), "signature count");
    if (signatureCount != 1)
    {
        throw std::runtime_error("Expected one LiteRT model signature");
    }
    LiteRtSignature signature = nullptr;
    check(LiteRtGetModelSignature(state.model, 0, &signature), "signature");
    LiteRtParamIndex inputCount = 0, outputCount = 0;
    check(LiteRtGetNumSignatureInputs(signature, &inputCount), "input count");
    check(LiteRtGetNumSignatureOutputs(signature, &outputCount), "output count");
    if (inputCount != 1 || outputCount != 1)
    {
        throw std::runtime_error("Expected one model input and output");
    }
    for (unsigned index = 0; index < 2; ++index)
    {
        LiteRtTensor tensor = nullptr;
        if (index == 0)
        {
            check(LiteRtGetSignatureInputTensorByIndex(signature, 0, &tensor), "input tensor");
        }
        else
        {
            check(LiteRtGetSignatureOutputTensorByIndex(signature, 0, &tensor), "output tensor");
        }
        LiteRtRankedTensorType type{};
        check(LiteRtGetRankedTensorType(tensor, &type), "tensor shape");
        if (type.element_type != kLiteRtElementTypeFloat32 || type.layout.rank != 2
            || type.layout.dimensions[0] != 1
            || (index == 0 ? type.layout.dimensions[1] != 69
                           : (type.layout.dimensions[1] != 3 && type.layout.dimensions[1] != 4)))
        {
            throw std::runtime_error("Expected float32 LiteRT tensors [1,69] -> [1,3 or 4]");
        }
        if (index == 1)
        {
            state.classes = static_cast<std::size_t>(type.layout.dimensions[1]);
        }
        LiteRtTensorBufferRequirements requirements = nullptr;
        if (index == 0)
        {
            check(
                LiteRtGetCompiledModelInputBufferRequirements(state.compiled, 0, 0, &requirements),
                "input requirements");
        }
        else
        {
            check(
                LiteRtGetCompiledModelOutputBufferRequirements(state.compiled, 0, 0, &requirements),
                "output requirements");
        }
        check(
            LiteRtCreateManagedTensorBufferFromRequirements(
                state.environment, &type, requirements, index == 0 ? &state.input : &state.output),
            "tensor buffer");
    }
    DrumFeatures quiet{};
    quiet[68] = 1.0f;
    if (!predict(quiet).valid)
    {
        throw std::runtime_error("LiteRT warm-up inference failed");
    }
}

LiteRtModel::~LiteRtModel() = default;

std::size_t LiteRtModel::classCount() const noexcept
{
    return implementation->classes;
}

DrumPrediction LiteRtModel::predict(const DrumFeatures& features) noexcept
{
    if (features[68] != 0.0f && features[68] != 1.0f)
    {
        return {};
    }
    for (auto value : features)
    {
        if (!std::isfinite(value))
        {
            return {};
        }
    }
    auto& state = *implementation;
    void* memory = nullptr;
    if (LiteRtLockTensorBuffer(state.input, &memory, kLiteRtTensorBufferLockModeWrite)
        != kLiteRtStatusOk)
    {
        return {};
    }
    std::memcpy(memory, features.data(), sizeof(features));
    if (LiteRtUnlockTensorBuffer(state.input) != kLiteRtStatusOk)
    {
        return {};
    }
    if (LiteRtRunCompiledModel(state.compiled, 0, 1, &state.input, 1, &state.output)
        != kLiteRtStatusOk)
    {
        return {};
    }
    if (LiteRtLockTensorBuffer(state.output, &memory, kLiteRtTensorBufferLockModeRead)
        != kLiteRtStatusOk)
    {
        return {};
    }
    DrumPrediction result;
    std::memcpy(result.probabilities.data(), memory, state.classes * sizeof(float));
    if (LiteRtUnlockTensorBuffer(state.output) != kLiteRtStatusOk)
    {
        return {};
    }
    float sum = 0.0f;
    for (auto value : result.probabilities)
    {
        if (!std::isfinite(value) || value < 0.0f || value > 1.001f)
        {
            return {};
        }
        sum += value;
    }
    if (std::abs(sum - 1.0f) > 0.001f
        || (features[68] == 0.0f
            && (result.probabilities[2] > 0.000001f || result.probabilities[3] > 0.000001f)))
    {
        return {};
    }
    result.classIndex = static_cast<std::size_t>(
        std::distance(result.probabilities.begin(),
                      std::max_element(result.probabilities.begin(), result.probabilities.end())));
    result.valid = true;
    return result;
}
} // namespace mimic
