#include "Fonts.hpp"
#include "TracyTexture.hpp"
#include "TracyMarkdown.hpp"
#include "TracyManualWindow.hpp"
#include "TracyAchievements.hpp"
#include "TracyDisassembly.hpp"
#include "TracyUserData.hpp"
namespace tracy
{
    class TracyLlmApi {};
    class TracyLlmChat {};
    class TracyLlmTools {};
}
#include "TracyLlm.hpp"

#include <imgui.h>

FontData g_fonts = {};
float FontNormal = 15.f;
float FontSmall = 10.f;
float FontBig = 22.f;
ImTextureID zigzagTex = 0;

void LoadFonts(float scale)
{
    (void)scale;
}

ImTextureID GetProfilerIconTexture()
{
    return (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts) ? ImGui::GetIO().Fonts->TexID.GetTexID() : 0;
}

namespace tracy
{
    void InitTexture()
    {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
        {
            zigzagTex = ImGui::GetIO().Fonts->TexID.GetTexID();
        }
    }
    ImTextureID MakeTexture(bool zigzag)
    {
        (void)zigzag;
        return (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts) ? ImGui::GetIO().Fonts->TexID.GetTexID() : 0;
    }
    void FreeTexture(ImTextureID tex, void(*runOnMainThread)(const std::function<void()>&, bool))
    {
        (void)tex;
        (void)runOnMainThread;
    }
    void UpdateTexture(ImTextureID tex, const char* data, int w, int h)
    {
        (void)tex; (void)data; (void)w; (void)h;
    }
    void UpdateTextureRGBA(ImTextureID tex, void* data, int w, int h)
    {
        (void)tex; (void)data; (void)w; (void)h;
    }
    void UpdateTextureRGBAMips(ImTextureID tex, void** data, int* w, int* h, size_t mips)
    {
        (void)tex; (void)data; (void)w; (void)h; (void)mips;
    }

    Markdown::Markdown(View* view, Worker* worker, ManualWindow* manualWindow)
        : m_parser(nullptr), m_view(view), m_worker(worker), m_manualWindow(manualWindow)
    {
    }

    Markdown::~Markdown() = default;

    void Markdown::Print(const char* str, size_t size)
    {
        if (str && size > 0)
        {
            ImGui::TextUnformatted(str, str + size);
        }
    }

    namespace
    {
        struct ManualState
        {
            size_t activeChunk = 0;
            bool positionReset = true;
            bool show = false;
        };
        static ManualState s_manState;
        static TracyManualData s_dummyData;
    }

    TracyManualData::TracyManualData() : m_hash(0) {}
    const TracyManualData& GetManualData() { return s_dummyData; }

    ManualWindow::ManualWindow(const TracyManualData& manual)
        : m_manual(manual)
        , m_activeChunk(s_manState.activeChunk)
        , m_positionReset(s_manState.positionReset)
        , m_show(s_manState.show)
    {
    }

    void ManualWindow::Draw(Markdown& md) { (void)md; }
    bool ManualWindow::Navigate(const char* anchor) { (void)anchor; return false; }
    const TracyManualData::ManualChunk* ManualWindow::GetChunk(const char* anchor) const { (void)anchor; return nullptr; }

    ManualWindow& GetManualWindow()
    {
        static ManualWindow window(s_dummyData);
        return window;
    }

    AchievementsMgr::AchievementsMgr() = default;
    AchievementsMgr::~AchievementsMgr() = default;
    void AchievementsMgr::Achieve(const char* id) { (void)id; }
    data::AchievementCategory** AchievementsMgr::GetCategories() const { return nullptr; }
    data::AchievementCategory* AchievementsMgr::GetCategoryForAchievement(const char* id) const { (void)id; return nullptr; }
    data::AchievementItem* AchievementsMgr::GetNextQueue() { return nullptr; }
    void AchievementsMgr::PopQueue() {}

    TracyLlm::TracyLlm(Worker& worker, View& view) : m_worker(worker), m_view(view) {}
    TracyLlm::~TracyLlm() = default;
    void TracyLlm::Draw(WindowConstraints& constraints) { (void)constraints; }
    void TracyLlm::AddAttachmentLocking(std::string&& str, const char* role) { (void)str; (void)role; }
    void TracyLlm::AddMessageLocking(std::string&& str, const char* role) { (void)str; (void)role; }
    bool TracyLlm::QueueSendMessageLocking() { return false; }
    bool TracyLlm::QueueFastMessageLocking(const nlohmann::json& req, std::function<void(nlohmann::json)> callback)
    {
        (void)req; (void)callback; return false;
    }

    DisasmData Disassemble(uint64_t symAddr, const Worker& worker)
    {
        (void)symAddr; (void)worker;
        DisasmData data;
        data.disasmFail = 1;
        return data;
    }

    std::string FormatDisassemblyLine(const AsmLine& opcode, Worker& worker, std::vector<std::string>& sources, uint64_t symAddr, const AddrStatData& as, const unordered_flat_map<uint64_t, uint32_t>& locMap)
    {
        (void)opcode; (void)worker; (void)sources; (void)symAddr; (void)as; (void)locMap;
        return {};
    }

    nlohmann::json JsonDisassembly(uint64_t symAddr, Worker& worker, const View& view)
    {
        (void)symAddr; (void)worker; (void)view;
        return {};
    }

    void GatherIpStats(uint64_t baseAddr, AddrStatData& as, const Worker& worker, bool limitView, const View& view, const char* filename, bool propagateInlines)
    {
        (void)baseAddr; (void)as; (void)worker; (void)limitView; (void)view; (void)filename; (void)propagateInlines;
    }

    void GatherAdditionalIpStats(uint64_t baseAddr, AddrStatData& as, Worker& worker, bool limitView, const View& view, const char* filename, bool propagateInlines)
    {
        (void)baseAddr; (void)as; (void)worker; (void)limitView; (void)view; (void)filename; (void)propagateInlines;
    }

    UserData::UserData() : m_time(0), m_preserveState(false), m_sidecarPublic(false) {}
    UserData::UserData(const char* program, uint64_t time, const char* filePath)
        : m_program(program ? program : ""), m_time(time), m_filePath(filePath ? filePath : ""), m_preserveState(false), m_sidecarPublic(false) {}
    void UserData::Init(const char* program, uint64_t time, const char* filePath)
    {
        m_program = program ? program : "";
        m_time = time;
        m_filePath = filePath ? filePath : "";
    }
    void UserData::SetFilePath(const char* filePath) { m_filePath = filePath ? filePath : ""; }
    void UserData::SetDescription(const char* description) { m_description = description ? description : ""; }
    void UserData::LoadState(ViewData& data) { data = m_viewData; }
    void UserData::StoreState(const ViewData& data) { m_viewData = data; }
    void UserData::StateShouldBePreserved() { m_preserveState = true; }
    void UserData::LoadAnnotations(std::vector<std::shared_ptr<Annotation>>& data) { data = m_annotations; }
    void UserData::StoreAnnotations(const std::vector<std::shared_ptr<Annotation>>& data) { m_annotations = data; }
    void UserData::LoadSourceSubstitutions(std::vector<SourceRegex>& data) { data = m_sourceSubstitutions; }
    void UserData::StoreSourceSubstitutions(const std::vector<SourceRegex>& data) { m_sourceSubstitutions = data; }
    bool UserData::Save() { return true; }
    void UserData::SetSidecarPublic(bool state) { m_sidecarPublic = state; }

    FILE* UserData::OpenFile(bool) { return nullptr; }
    FILE* UserData::OpenFileLegacy(const char*) { return nullptr; }
    std::string UserData::GetSidecarPath(bool) const { return {}; }
    bool UserData::Load() { return false; }
    void UserData::LoadLegacyDescription() {}
    void UserData::LoadLegacyState() {}
    void UserData::LoadLegacyAnnotations() {}
    void UserData::LoadLegacySourceSubstitutions() {}
}

