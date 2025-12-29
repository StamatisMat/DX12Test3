#include "ShaderCompiler.h"

SlangCompiler::SlangCompiler()
{
    // Create a global session that can be reused.
    slang::createGlobalSession(&desc, m_globalSession.writeRef());
}

SlangCompiler::~SlangCompiler()
{
    /*
    if (m_globalSession)
    {
        m_globalSession->release();
    }
    */
}

// Compile to GLSL text - returns all entry points
std::vector<ShaderOutput> SlangCompiler::compileToGLSL(const std::string& source,
    const std::vector<std::string>& entryPoints, const std::string& path)
{
    return compile(source, entryPoints, SLANG_GLSL, path);
}

// Compile to HLSL text - returns all entry points
std::vector<ShaderOutput> SlangCompiler::compileToHLSL(const std::string& source,
    const std::vector<std::string>& entryPoints, const std::string& path)
{
    return compile(source, entryPoints, SLANG_HLSL, path);
}

// Compile to SPIR-V binary - returns all entry points
std::vector<ShaderOutput> SlangCompiler::compileToSPIRV(const std::string& source,
    const std::vector<std::string>& entryPoints, const std::string& path)
{
    return compile(source, entryPoints, SLANG_SPIRV, path);
}

// Convenience overloads for single entry point
std::string SlangCompiler::compileToGLSLSingle(const std::string& source,
    const std::string& entryPoint, const std::string& path)
{
    std::vector<ShaderOutput> outputs = compileToGLSL(source, { entryPoint }, path);
    if (outputs.empty()) return "";
    return outputs[0].asText();
}

std::string SlangCompiler::compileToHLSLSingle(const std::string& source,
    const std::string& entryPoint, const std::string& path)
{
    std::vector<ShaderOutput> outputs = compileToHLSL(source, { entryPoint }, path);
    if (outputs.empty()) return "";
    return outputs[0].asText();
}

std::vector<uint8_t> SlangCompiler::compileToSPIRVSingle(const std::string& source,
    const std::string& entryPoint, const std::string& path)
{
    std::vector<ShaderOutput> outputs = compileToSPIRV(source, { entryPoint }, path);
    if (outputs.empty()) return std::vector<uint8_t>();
    return outputs[0].binaryData;
}

std::vector<ShaderOutput> SlangCompiler::compile(const std::string& source,
    const std::vector<std::string>& entryPoints,
    SlangCompileTarget target, const std::string& path)
{
    std::vector<ShaderOutput> outputs;

    if (entryPoints.empty())
    {
        throw std::runtime_error("No entry points specified");
    }

    // Setup session descriptor
    slang::SessionDesc sessionDesc{};
    slang::TargetDesc targetDesc{};
    Slang::ComPtr<slang::ISession> session;

    targetDesc.format = target;
    targetDesc.profile = m_globalSession->findProfile("sm_6_0");

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    const char* searchPaths[] = { "./", "../shaders/", "../../shaders/" };
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = 3;

    // Create session
    SlangResult result = m_globalSession->createSession(sessionDesc, session.writeRef());
    if (SLANG_FAILED(result) || !session)
    {
        throw std::runtime_error("Failed to create Slang session");
    }

    // Load module from source string
    Slang::ComPtr<slang::IBlob> diagnostics;

    slang::IModule* loadedModule = session->loadModuleFromSourceString(
        "shader",
        path.c_str(),
        source.c_str(),
        diagnostics.writeRef());

    if (diagnostics && diagnostics->getBufferSize() > 0)
    {
        std::string diagStr((const char*)diagnostics->getBufferPointer(),
            diagnostics->getBufferSize());
        if (!diagStr.empty())
        {
            std::cerr << "Slang diagnostics:\n" << diagStr << "\n";
        }
    }

    if (!loadedModule)
    {
        throw std::runtime_error("Failed to load Slang module from source");
    }

    // Find all entry points
    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointObjs;
    for (const auto& entryPointName : entryPoints)
    {
        Slang::ComPtr<slang::IEntryPoint> entryPointObj;
        loadedModule->findEntryPointByName(entryPointName.c_str(), entryPointObj.writeRef());

        if (!entryPointObj)
        {
            throw std::runtime_error("Failed to find entry point: " + entryPointName);
        }

        entryPointObjs.push_back(entryPointObj);
    }

    // Create composite component type (module + all entry points)
    std::vector<slang::IComponentType*> components;
    components.push_back(loadedModule);
    for (auto& ep : entryPointObjs)
    {
        components.push_back(ep.get());
    }

    Slang::ComPtr<slang::IComponentType> program;
    session->createCompositeComponentType(
        components.data(),
        (SlangInt)components.size(),
        program.writeRef(),
        diagnostics.writeRef());

    if (!program)
    {
        throw std::runtime_error("Failed to create composite component type");
    }

    // Link the program once for all entry points
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    program->link(linkedProgram.writeRef(), diagnostics.writeRef());

    if (diagnostics)
    {
        std::string diagStr((const char*)diagnostics->getBufferPointer(),
            diagnostics->getBufferSize());
        if (!diagStr.empty())
        {
            std::cerr << "Slang link diagnostics:\n" << diagStr << "\n";
        }
    }

    if (!linkedProgram)
    {
        throw std::runtime_error("Failed to link program");
    }

    // Get compiled code for each entry point
    int targetIndex = 0;
    for (size_t i = 0; i < entryPoints.size(); ++i)
    {
        Slang::ComPtr<slang::IBlob> codeBlob;

        linkedProgram->getEntryPointCode(
            (int)i,
            targetIndex,
            codeBlob.writeRef(),
            diagnostics.writeRef());

        if (!codeBlob)
        {
            std::cerr << "Failed to get code for entry point: " << entryPoints[i] << "\n";
            continue;
        }

        // Store output
        ShaderOutput output;
        output.target = target;
        output.entryPointName = entryPoints[i];
        const uint8_t* data = static_cast<const uint8_t*>(codeBlob->getBufferPointer());
        uint64_t size = codeBlob->getBufferSize();
        output.binaryData.assign(data, data + size);

        outputs.push_back(output);
    }

    return outputs;
}
