// ========== 修改 ==========
// 隐藏控制台窗口的核心原理：
// 1. #pragma comment(linker, "/SUBSYSTEM:WINDOWS") 强制链接为 Windows 应用程序（无控制台）
// 2. 入口函数从 main() 改为 WinMain()（Windows 应用程序的标准入口）
// 3. Debug 模式下通过 AllocConsole() 手动分配控制台用于调试，Release 模式完全无窗口
// 4. 角色叠加窗口增加就绪标志，防止 UpdateLayeredWindow 设置内容前闪现黑窗
// ========== 修改结束 ==========

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <dwmapi.h>          // ========== 修改：DWM 透明窗口支持 ==========
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwmapi.lib")  // ========== 修改：DWM 库链接 ==========

// ========== 修改 ==========
// 强制链接为 Windows 子系统，消除控制台窗口
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
// ========== 修改结束 ==========

// ====== D3D 全局变量 ======
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ====== 窗口句柄 ======
static HWND g_hWnd = nullptr;

// ====== 角色装饰全局变量 ======
static HWND g_charOverlayHwnd = nullptr;
static IWICBitmapSource* g_wicOriginal = nullptr;
static int g_charOrigW = 0, g_charOrigH = 0;
static HBITMAP g_charHBitmap = nullptr;
static int g_charBmpW = 0, g_charBmpH = 0;
static ID3D11ShaderResourceView* g_charTexSRV = nullptr;
// ========== 修改 ==========
static bool g_charOverlayReady = false;  // 叠加窗口就绪标志：防止 UpdateLayeredWindow 设置内容前闪现黑窗
// ========== 修改结束 ==========

// ====== 丛雨主题配色 ======
#define COLOR_BG            ImVec4(0.078f, 0.110f, 0.102f, 1.00f)
#define COLOR_BG_LIGHT      ImVec4(0.098f, 0.133f, 0.125f, 1.00f)
#define COLOR_BG_CARD       ImVec4(0.110f, 0.149f, 0.137f, 1.00f)
#define COLOR_PRIMARY       ImVec4(0.278f, 0.451f, 0.361f, 1.00f)
#define COLOR_PRIMARY_HOVER ImVec4(0.325f, 0.510f, 0.412f, 1.00f)
#define COLOR_PRIMARY_ACTIVE ImVec4(0.235f, 0.392f, 0.310f, 1.00f)
#define COLOR_ACCENT        ImVec4(0.549f, 0.180f, 0.220f, 1.00f)
#define COLOR_ACCENT_HOVER  ImVec4(0.627f, 0.220f, 0.263f, 1.00f)
#define COLOR_ACCENT_ACTIVE ImVec4(0.471f, 0.145f, 0.180f, 1.00f)
#define COLOR_TEXT          ImVec4(0.820f, 0.902f, 0.843f, 1.00f)
#define COLOR_TEXT_DIM      ImVec4(0.510f, 0.580f, 0.533f, 1.00f)
#define COLOR_BORDER        ImVec4(0.176f, 0.220f, 0.200f, 1.00f)

// ====== 布局常量 ======
const float SIDEBAR_WIDTH = 220.0f;
// ========== 修复300px巨大间距 ==========
// 角色重叠比例：叠加窗口向右偏移（覆盖主窗口）的比例。
// 增大此值让角色靠近主窗口。0.0=无偏移（原始位置），0.6=右移60%图片宽度
// 典型值：0.4-0.7，直到角色紧贴窗口左边缘
const float CHARACTER_OVERLAP_RATIO = 0.48f;
// ========== 修复结束 ==========
// ========== 修改 ==========
// 登录卡片尺寸 + 主窗口尺寸，用于透明卡片 → 主界面切换
const int LOGIN_CARD_W = 780, LOGIN_CARD_H = 520;
const int MAIN_WIN_W = 920, MAIN_WIN_H = 620;
static bool g_was_logged_in = false;  // 追踪登录状态变化以触发窗口 resize
// ========== 修改结束 ==========

// ====== 登录状态 ======
static bool g_logged_in = false;
static char g_key_input[64] = {};
static const char* g_correct_key = "admin123";
static bool g_key_error = false;

// ====== 主界面标签页 ======
static int g_selected_tab = 0;
static int g_language = 0; // 0=中文, 1=English

// 翻译辅助
inline const char* T(const char* zh, const char* en) { return g_language == 0 ? zh : en; }

// ====== 功能1：数据面板 ======
static int g_counter = 0;
static float g_slider_val = 0.5f;
static bool g_check_box = false;
static char g_text_input[128] = "Hello";

// ====== 功能2：列表管理 ======
static char g_item_name[64] = {};
static std::vector<std::string> g_items;
static int g_prev_item_count = 0;  // 用于检测新增项

// ====== 功能3：状态监控 ======
static float g_fps = 0.0f;
static float g_fps_timer = 0.0f;
static int g_frame_count = 0;

// ====== 主题控制 ======
static ImVec4 g_accent_color  = COLOR_ACCENT;
static ImVec4 g_primary_color = COLOR_PRIMARY;
static float g_rounding = 8.0f;

// ====== 动画系统 ======
#include <cmath>

// 弹簧动画：平滑过渡浮点值，带物理弹性
struct AnimFloat {
    float target = 0.0f;
    float current = 0.0f;
    float velocity = 0.0f;
    float stiffness = 120.0f;
    float damping = 14.0f;

    AnimFloat() {}
    AnimFloat(float v) : target(v), current(v) {}

    void Set(float t, bool instant = false) {
        target = t;
        if (instant) { current = t; velocity = 0.0f; }
    }

    float Update() {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.1f) dt = 0.1f;
        float force = (target - current) * stiffness;
        velocity += force * dt;
        velocity *= expf(-damping * dt);
        current += velocity * dt;
        if (fabsf(target - current) < 0.0005f && fabsf(velocity) < 0.01f) {
            current = target;
            velocity = 0.0f;
        }
        return current;
    }

    float Val() const { return current; }
    operator float() { return Val(); }
};

// 缓动插值：快速接近目标，无弹性
struct AnimSmooth {
    float target = 0.0f;
    float current = 0.0f;
    float speed = 8.0f;

    AnimSmooth() {}
    AnimSmooth(float v) : target(v), current(v) {}

    void Set(float t, bool instant = false) {
        target = t;
        if (instant) { current = t; velocity = 0.0f; }
    }
    float Update() {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.1f) dt = 0.1f;
        current += (target - current) * speed * dt;
        if (fabsf(target - current) < 0.0005f) current = target;
        return current;
    }
    float Val() const { return current; }
    operator float() { return Val(); }

private:
    float velocity = 0.0f;
};

// 颜色动画
struct AnimColor {
    ImVec4 target = ImVec4(0,0,0,1);
    ImVec4 current = ImVec4(0,0,0,1);
    float speed = 12.0f;

    AnimColor() {}
    AnimColor(const ImVec4& c) : target(c), current(c) {}

    void Set(const ImVec4& t) { target = t; }

    ImVec4 Update() {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.1f) dt = 0.1f;
        float f = 1.0f - expf(-speed * dt);
        current.x += (target.x - current.x) * f;
        current.y += (target.y - current.y) * f;
        current.z += (target.z - current.z) * f;
        current.w += (target.w - current.w) * f;
        return current;
    }
    ImVec4 Val() const { return current; }
    operator ImVec4() { return Val(); }
};

// ====== 动画状态 ======
static AnimFloat  g_tab_offset;           // 标签页水平滑动偏移
static AnimFloat  g_counter_scale;        // 计数器文字缩放
static AnimFloat  g_progress_fill;        // 进度条填充
static AnimFloat  g_login_elements[5];    // 登录页5个元素淡入
static AnimFloat  g_tab_btn_alpha[4];     // 4个标签按钮高亮 alpha
static AnimSmooth  g_card_hover[12];      // 卡片悬停缩放 (0~1)
static AnimSmooth  g_list_heights[20];    // 列表项高度动画
static AnimColor  g_tab_btn_color[4];     // 标签按钮颜色过渡
static AnimSmooth  g_button_scale;        // 登录按钮缩放
static AnimFloat  g_main_enter_scale;     // 主界面入场缩放 (0.8→1.0 弹跳)
static AnimFloat  g_main_enter_slide;     // 主界面入场上滑偏移
static float       g_prev_counter = 0;    // 上一次计数器值

// 初始化动画状态
static void InitAnimations() {
    // 登录元素默认可见，退出登录时才触发淡入动画
    for (int i = 0; i < 5; i++) g_login_elements[i].Set(1.0f, true);
    g_counter_scale.Set(1.0f, true);
    g_button_scale.Set(1.0f, true);
    for (int i = 0; i < 4; i++) {
        g_tab_btn_alpha[i].Set(i == 0 ? 1.0f : 0.0f, true);
        g_tab_btn_color[i].Set(i == 0 ? COLOR_ACCENT : ImVec4(0,0,0,0));
    }
    for (int i = 0; i < 12; i++) g_card_hover[i].Set(0.0f, true);
    for (int i = 0; i < 20; i++) g_list_heights[i].Set(1.0f, true);
    g_main_enter_scale.Set(0.85f, true);
    g_main_enter_slide.Set(40.0f, true);
}

// 更新所有持续动画
static void UpdateAnimations() {
    g_tab_offset.Update();
    g_counter_scale.Update();
    g_progress_fill.Update();
    g_button_scale.Update();
    for (int i = 0; i < 5; i++) g_login_elements[i].Update();
    for (int i = 0; i < 4; i++) { g_tab_btn_alpha[i].Update(); g_tab_btn_color[i].Update(); }
    for (int i = 0; i < 12; i++) g_card_hover[i].Update();
    for (int i = 0; i < 20; i++) g_list_heights[i].Update();
    g_main_enter_scale.Update();
    g_main_enter_slide.Update();
}

// 缓动函数
inline float EaseOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float EaseInOut(float t) { return t < 0.5f ? 2*t*t : -1+(4-2*t)*t; }

// ========== 修改 ==========
// Debug 模式调试输出封装 (替代 printf/cout，无控制台时仍可通过 DebugView 查看)
#ifdef _DEBUG
    #define DBG_PRINT(fmt, ...) do { \
        char _dbg_buf[512]; \
        snprintf(_dbg_buf, sizeof(_dbg_buf), fmt, ##__VA_ARGS__); \
        OutputDebugStringA(_dbg_buf); \
    } while(0)
#else
    #define DBG_PRINT(fmt, ...) ((void)0)
#endif
// ========== 修改结束 ==========

// ====== 丛雨主题 ======
void ApplyMurasameTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    style.WindowRounding    = g_rounding;
    style.ChildRounding     = g_rounding;
    style.FrameRounding     = g_rounding;
    style.PopupRounding     = g_rounding;
    style.ScrollbarRounding = g_rounding;
    style.GrabRounding      = g_rounding;
    style.TabRounding       = g_rounding;

    style.WindowPadding   = ImVec2(16, 16);
    style.FramePadding    = ImVec2(10, 6);
    style.ItemSpacing     = ImVec2(10, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing   = 20.0f;
    style.ScrollbarSize   = 12.0f;
    style.GrabMinSize     = 12.0f;

    c[ImGuiCol_WindowBg]         = COLOR_BG;
    c[ImGuiCol_ChildBg]          = COLOR_BG_LIGHT;
    c[ImGuiCol_PopupBg]          = COLOR_BG_LIGHT;
    c[ImGuiCol_Text]             = COLOR_TEXT;
    c[ImGuiCol_TextDisabled]     = COLOR_TEXT_DIM;
    c[ImGuiCol_Border]           = COLOR_BORDER;
    c[ImGuiCol_BorderShadow]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]          = ImVec4(0.125f, 0.165f, 0.149f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.157f, 0.204f, 0.184f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.188f, 0.243f, 0.220f, 1.00f);
    c[ImGuiCol_TitleBg]          = COLOR_BG;
    c[ImGuiCol_TitleBgActive]    = COLOR_BG_LIGHT;
    c[ImGuiCol_TitleBgCollapsed] = COLOR_BG;
    c[ImGuiCol_Button]           = COLOR_PRIMARY;
    c[ImGuiCol_ButtonHovered]    = COLOR_PRIMARY_HOVER;
    c[ImGuiCol_ButtonActive]     = COLOR_PRIMARY_ACTIVE;
    c[ImGuiCol_Header]           = COLOR_ACCENT;
    c[ImGuiCol_HeaderHovered]    = COLOR_ACCENT_HOVER;
    c[ImGuiCol_HeaderActive]     = COLOR_ACCENT_ACTIVE;
    c[ImGuiCol_Tab]              = COLOR_BG_CARD;
    c[ImGuiCol_TabHovered]       = COLOR_PRIMARY_HOVER;
    c[ImGuiCol_TabActive]        = COLOR_ACCENT;
    c[ImGuiCol_TabUnfocused]     = COLOR_BG_CARD;
    c[ImGuiCol_TabUnfocusedActive] = COLOR_ACCENT;
    c[ImGuiCol_Separator]        = COLOR_BORDER;
    c[ImGuiCol_SeparatorHovered] = COLOR_PRIMARY;
    c[ImGuiCol_SeparatorActive]  = COLOR_ACCENT;
    c[ImGuiCol_ScrollbarBg]          = COLOR_BG;
    c[ImGuiCol_ScrollbarGrab]        = COLOR_PRIMARY;
    c[ImGuiCol_ScrollbarGrabHovered] = COLOR_PRIMARY_HOVER;
    c[ImGuiCol_ScrollbarGrabActive]  = COLOR_PRIMARY_ACTIVE;
    c[ImGuiCol_ResizeGrip]        = COLOR_PRIMARY;
    c[ImGuiCol_ResizeGripHovered] = COLOR_PRIMARY_HOVER;
    c[ImGuiCol_ResizeGripActive]  = COLOR_ACCENT;
    c[ImGuiCol_SliderGrab]        = COLOR_PRIMARY;
    c[ImGuiCol_SliderGrabActive]  = COLOR_PRIMARY_HOVER;
    c[ImGuiCol_CheckMark]         = COLOR_PRIMARY;
    c[ImGuiCol_PlotLines]         = COLOR_PRIMARY;
    c[ImGuiCol_PlotLinesHovered]  = COLOR_ACCENT;
    c[ImGuiCol_PlotHistogram]     = COLOR_PRIMARY;
    c[ImGuiCol_PlotHistogramHovered] = COLOR_ACCENT;
    c[ImGuiCol_NavHighlight]      = COLOR_ACCENT;
    c[ImGuiCol_DragDropTarget]    = COLOR_ACCENT;
}

// 卡片 hover 动画索引
static int g_card_hover_idx = 0;

// 卡片
void BeginCard(const char* title, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, g_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLOR_BG_CARD);
    // 获取 hover 动画索引
    int cardId = g_card_hover_idx % 12;
    float hoverAlpha = g_card_hover[cardId].Val();
    // hover 时边框高亮
    ImVec4 borderColor = ImVec4(
        COLOR_BORDER.x + (COLOR_ACCENT.x - COLOR_BORDER.x) * hoverAlpha,
        COLOR_BORDER.y + (COLOR_ACCENT.y - COLOR_BORDER.y) * hoverAlpha,
        COLOR_BORDER.z + (COLOR_ACCENT.z - COLOR_BORDER.z) * hoverAlpha,
        1.0f
    );
    ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
    ImGui::BeginChild(title, size, true);
    g_card_hover[cardId].Set(ImGui::IsWindowHovered() ? 1.0f : 0.0f);
    g_card_hover_idx++;
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void EndCard() {
    ImGui::EndChild();
}

// ====== 登录页面 ======
// 紧凑布局 + 垂直均匀分布 + 逐项淡入动画
void DrawLoginPage() {
    ImVec2 cardSize = ImVec2((float)LOGIN_CARD_W, (float)LOGIN_CARD_H);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(cardSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(48, 24));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("Login", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::PopStyleVar(3);

    float winW = ImGui::GetWindowWidth();
    float winH = ImGui::GetWindowHeight();
    float sidePad = 48.0f;
    float inputW = winW - sidePad * 2;

    // 右上角樱花 emoji
    {
        ImVec2 cursor_backup = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(winW - 40, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_login_elements[0].Val());
        ImGui::Text("\xF0\x9F\x8C\xB8");
        ImGui::PopStyleVar();
        ImGui::SetCursorPos(cursor_backup);
    }

    // ── 标题区 ──
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_login_elements[0].Val());
    ImGui::SetCursorPosY(winH * 0.04f);
    ImGui::SetWindowFontScale(1.8f);
    ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.57f, 1.00f), "Login");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_login_elements[1].Val());
    ImGui::SetCursorPosY(winH * 0.19f);
    ImGui::TextColored(COLOR_PRIMARY, T("\xe8\xba\xab\xe4\xbb\xbd\xe9\xaa\x8c\xe8\xaf\x81", "Authentication"));
    ImGui::SetCursorPosY(winH * 0.23f);
    ImGui::SetCursorPosX(sidePad);
    ImGui::Separator();
    ImGui::SetCursorPosX(0);
    ImGui::PopStyleVar();

    // ── 输入区 ──
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_login_elements[2].Val());
    ImGui::SetCursorPos(ImVec2(sidePad, winH * 0.38f));
    ImGui::Text(T("\xe8\xae\xbf\xe9\x97\xae\xe5\xaf\x86\xe9\x92\xa5:", "Access Key:"));

    ImGui::SetCursorPos(ImVec2(sidePad, winH * 0.44f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 10));
    ImGui::PushItemWidth(inputW);
    ImGui::InputText("##key_input", g_key_input, sizeof(g_key_input),
        ImGuiInputTextFlags_Password);
    bool input_active = ImGui::IsItemActive();
    ImVec2 input_min = ImGui::GetItemRectMin();
    ImVec2 input_max = ImGui::GetItemRectMax();
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);

    if (input_active) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(ImVec2(input_min.x - 1, input_min.y - 1),
                    ImVec2(input_max.x + 1, input_max.y + 1),
                    IM_COL32(140, 46, 56, 255), 10.0f, 0, 2.0f);
    }

    // 错误提示
    if (g_key_error) {
        ImGui::SetCursorPos(ImVec2(sidePad, winH * 0.56f));
        ImGui::TextColored(COLOR_ACCENT, T("\xe5\xaf\x86\xe9\x92\xa5\xe9\x94\x99\xe8\xaf\xaf\xef\xbc\x8c\xe8\xaf\xb7\xe9\x87\x8d\xe8\xaf\x95\xe3\x80\x82", "Invalid key, please try again."));
    }
    ImGui::PopStyleVar();

    // ── 按钮区 ──
    float btnAlpha = EaseOut(g_login_elements[4].Val());
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, btnAlpha);
    ImGui::SetCursorPos(ImVec2(sidePad, winH * 0.66f));

    float btnScale = g_button_scale.Val();
    float btnW = inputW * btnScale;
    float btnPadX = (inputW - btnW) * 0.5f;
    ImGui::SetCursorPos(ImVec2(sidePad + btnPadX, winH * 0.66f));

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 14));
    if (ImGui::Button(T("\xe7\x99\xbb\xe5\xbd\x95", "Log In"), ImVec2(btnW, 0))) {
        if (strcmp(g_key_input, g_correct_key) == 0) {
            g_logged_in = true;
            g_key_error = false;
            memset(g_key_input, 0, sizeof(g_key_input));
            // 触发主界面入场动画
            g_main_enter_scale.Set(0.85f, true);
            g_main_enter_scale.Set(1.0f);
            g_main_enter_slide.Set(40.0f, true);
            g_main_enter_slide.Set(0.0f);
            DBG_PRINT("[Login] User authenticated successfully\n");
        } else {
            g_key_error = true;
            g_button_scale.Set(0.92f);
            DBG_PRINT("[Login] Authentication failed\n");
        }
    }
    bool btn_hovered = ImGui::IsItemHovered();
    ImVec2 btn_min = ImGui::GetItemRectMin();
    ImVec2 btn_max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar(2);

    if (btn_hovered) {
        g_button_scale.Set(1.03f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(btn_min, btn_max,
            IM_COL32(114, 179, 146, 220), 10.0f, 0, 2.0f);
    } else if (!g_key_error) {
        g_button_scale.Set(1.0f);
    }

    ImGui::PopStyleVar();

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && strlen(g_key_input) > 0) {
        if (strcmp(g_key_input, g_correct_key) == 0) {
            g_logged_in = true;
            g_key_error = false;
            memset(g_key_input, 0, sizeof(g_key_input));
            // 触发主界面入场动画
            g_main_enter_scale.Set(0.85f, true);
            g_main_enter_scale.Set(1.0f);
            g_main_enter_slide.Set(40.0f, true);
            g_main_enter_slide.Set(0.0f);
        } else {
            g_key_error = true;
            g_button_scale.Set(0.92f);
        }
    }

    ImGui::End();
}

// ====== 主界面 ======
void DrawMainUI() {
    ImVec2 display_size = ImGui::GetIO().DisplaySize;

    // 入场动画：缩放 + 上滑
    float enterScale = g_main_enter_scale.Val();
    float enterSlideY = g_main_enter_slide.Val();
    float enterAlpha = enterScale;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);

    ImGui::Begin("MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings);

    // 入场动画偏移 + 透明度
    ImVec2 baseCursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(baseCursor.x, baseCursor.y + enterSlideY));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, enterAlpha);

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, g_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLOR_BG_LIGHT);
    ImGui::BeginChild("##sidebar", ImVec2(SIDEBAR_WIDTH, 0), true);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, COLOR_PRIMARY);
    ImGui::Text("Murasame");
    ImGui::PopStyleColor();
    ImGui::TextColored(COLOR_TEXT_DIM, T("\xe5\xaf\xbc\xe8\x88\xaa", "Navigation"));
    ImGui::Separator();
    ImGui::Spacing();

    const char* tabs[] = { T("\xe6\x95\xb0\xe6\x8d\xae\xe9\x9d\xa2\xe6\x9d\xbf", "Data Panel"),
                           T("\xe5\x88\x97\xe8\xa1\xa8\xe7\xae\xa1\xe7\x90\x86", "List Manager"),
                           T("\xe7\x9b\x91\xe6\x8e\xa7\xe9\x9d\xa2\xe6\x9d\xbf", "Monitor"),
                           T("\xe8\xae\xbe\xe7\xbd\xae", "Settings") };
    for (int i = 0; i < 4; i++) {
        // 使用动画颜色平滑过渡选中态
        ImVec4 activeColor = g_tab_btn_color[i].Val();
        float alpha = activeColor.w;
        if (alpha > 0.01f) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(activeColor.x, activeColor.y, activeColor.z, alpha * 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_ACCENT_HOVER);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, COLOR_ACCENT_ACTIVE);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(COLOR_PRIMARY.x, COLOR_PRIMARY.y, COLOR_PRIMARY.z, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(COLOR_PRIMARY.x, COLOR_PRIMARY.y, COLOR_PRIMARY.z, 0.50f));
        }
        if (ImGui::Button(tabs[i], ImVec2(-1, 38))) {
            if (g_selected_tab != i) {
                // 旧标签动画退出
                g_tab_btn_color[g_selected_tab].Set(ImVec4(0,0,0,0));
                g_tab_btn_alpha[g_selected_tab].Set(0.0f);
                // 新标签动画进入
                g_tab_btn_color[i].Set(COLOR_ACCENT);
                g_tab_btn_alpha[i].Set(1.0f);
                g_tab_offset.Set((float)(i - g_selected_tab) * 30.0f);
                g_selected_tab = i;
            }
        }
        ImGui::PopStyleColor(3);
    }
    g_tab_offset.Set(0.0f);  // 归零目标，产生回弹效果

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, COLOR_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_ACCENT_HOVER);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, COLOR_ACCENT_ACTIVE);
    if (ImGui::Button(T("\xe9\x80\x80\xe5\x87\xba\xe7\x99\xbb\xe5\xbd\x95", "Logout"), ImVec2(-1, 32))) {
        g_logged_in = false;
        g_key_error = false;
        g_selected_tab = 0;
        // 触发登录页淡入动画（延时错开）
        for (int i = 0; i < 5; i++) g_login_elements[i].Set(0.0f, true);
        for (int i = 0; i < 5; i++) g_login_elements[i].Set(1.0f);
        g_login_elements[0].damping = 10.0f;
        g_login_elements[1].damping = 12.0f;
        g_login_elements[2].damping = 14.0f;
        g_login_elements[3].damping = 16.0f;
        g_login_elements[4].damping = 18.0f;
    }
    ImGui::PopStyleColor(3);

    ImGui::EndChild();

    ImGui::SameLine(0, 10);

    ImGui::BeginChild("##content", ImVec2(0, 0), true);

    // 标签页切换水平滑动动画
    float slideOffset = g_tab_offset.Val();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slideOffset);

    switch (g_selected_tab) {
    case 0: {
        ImGui::TextColored(COLOR_PRIMARY, T("\xe6\x95\xb0\xe6\x8d\xae\xe9\x9d\xa2\xe6\x9d\xbf", "Data Panel"));
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe6\x8e\xa7\xe4\xbb\xb6\xe5\x92\x8c\xe8\xbe\x93\xe5\x85\xa5\xe6\xbc\x94\xe7\xa4\xba", "Control widgets and input demonstration"));
        ImGui::Spacing();

        BeginCard("##card_counter");
        ImGui::Text(T("\xe8\xae\xa1\xe6\x95\xb0\xe5\x99\xa8", "Counter"));
        ImGui::Spacing();
        // 计数器值带弹性缩放动画
        if (g_counter != g_prev_counter) {
            g_counter_scale.Set(1.25f);
            g_prev_counter = (float)g_counter;
        }
        float cntScale = g_counter_scale.Val();
        ImGui::SetWindowFontScale(cntScale);
        ImGui::TextColored(COLOR_ACCENT, "%d", g_counter);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
        if (ImGui::Button(" + ", ImVec2(40, 28))) g_counter++;
        ImGui::SameLine();
        if (ImGui::Button(" - ", ImVec2(40, 28))) g_counter--;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, COLOR_ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_ACCENT_HOVER);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, COLOR_ACCENT_ACTIVE);
        if (ImGui::Button(T(" \xe9\x87\x8d\xe7\xbd\xae ", " Reset "), ImVec2(60, 28))) g_counter = 0;
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_slider");
        ImGui::Text(T("\xe6\xbb\x91\xe5\x9d\x97\xe6\x8e\xa7\xe4\xbb\xb6", "Slider Control"));
        ImGui::Spacing();
        ImGui::Text(T("\xe5\xbd\x93\xe5\x89\x8d\xe5\x80\xbc: %.2f", "Value: %.2f"), g_slider_val);
        ImGui::SliderFloat("##slider", &g_slider_val, 0.0f, 1.0f);
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_checkbox");
        ImGui::Text(T("\xe5\xbc\x80\xe5\x85\xb3\xe4\xb8\x8e\xe8\xbe\x93\xe5\x85\xa5", "Toggle & Input"));
        ImGui::Spacing();
        ImGui::Checkbox(T("\xe5\x90\xaf\xe7\x94\xa8\xe5\x8a\x9f\xe8\x83\xbd", "Enable Feature"), &g_check_box);
        ImGui::Spacing();
        ImGui::Text(T("\xe6\x96\x87\xe6\x9c\xac\xe8\xbe\x93\xe5\x85\xa5:", "Text Input:"));
        ImGui::Spacing();
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##text_input", g_text_input, sizeof(g_text_input));
        ImGui::PopItemWidth();
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe8\xbe\x93\xe5\x87\xba: %s", "Output: %s"), g_text_input);
        EndCard();
        break;
    }
    case 1: {
        ImGui::TextColored(COLOR_PRIMARY, T("\xe5\x88\x97\xe8\xa1\xa8\xe7\xae\xa1\xe7\x90\x86", "List Manager"));
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe6\xb7\xbb\xe5\x8a\xa0\xe3\x80\x81\xe5\x88\xa0\xe9\x99\xa4\xe5\x92\x8c\xe7\xae\xa1\xe7\x90\x86\xe9\xa1\xb9\xe7\x9b\xae", "Add, remove and manage items"));
        ImGui::Spacing();

        BeginCard("##card_add");
        ImGui::Text(T("\xe6\xb7\xbb\xe5\x8a\xa0\xe6\x96\xb0\xe9\xa1\xb9", "Add New Item"));
        ImGui::Spacing();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 70);
        ImGui::InputText("##item_name", g_item_name, sizeof(g_item_name));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(T("\xe6\xb7\xbb\xe5\x8a\xa0", "Add"), ImVec2(60, 0)) && strlen(g_item_name) > 0) {
            g_items.push_back(g_item_name);
            memset(g_item_name, 0, sizeof(g_item_name));
            // 新项淡入动画
            int idx = (int)g_items.size() - 1;
            if (idx < 20) g_list_heights[idx].Set(0.0f, true);
            if (idx < 20) g_list_heights[idx].Set(1.0f);
        }
        EndCard();

        // 检测列表数量变化
        if ((int)g_items.size() > g_prev_item_count) g_prev_item_count = (int)g_items.size();
        if ((int)g_items.size() < g_prev_item_count) g_prev_item_count = (int)g_items.size();

        ImGui::Spacing();
        BeginCard("##card_list");
        ImGui::Text(T("\xe9\xa1\xb9\xe7\x9b\xae (%d)", "Items (%d)"), (int)g_items.size());
        ImGui::Spacing();
        ImGui::BeginChild("##item_list", ImVec2(0, 180), true);
        for (int i = 0; i < (int)g_items.size(); i++) {
            // 新增项淡入
            float itemAlpha = (i < 20) ? EaseOut(g_list_heights[i].Val()) : 1.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, itemAlpha);
            char label[128];
            snprintf(label, sizeof(label), "%02d.  %s", i + 1, g_items[i].c_str());
            ImGui::Text("%s", label);
            snprintf(label, sizeof(label), "Del##%d", i);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 55);
            ImGui::PushStyleColor(ImGuiCol_Button, COLOR_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_ACCENT_HOVER);
            if (ImGui::Button(label, ImVec2(48, 20))) {
                g_items.erase(g_items.begin() + i);
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
                ImGui::EndChild();
                EndCard();
                break;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        EndCard();

        if (!g_items.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, COLOR_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_ACCENT_HOVER);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, COLOR_ACCENT_ACTIVE);
            if (ImGui::Button(T("\xe6\xb8\x85\xe7\xa9\xba\xe5\x85\xa8\xe9\x83\xa8", "Clear All"), ImVec2(-1, 28))) {
                g_items.clear();
            }
            ImGui::PopStyleColor(3);
        }
        break;
    }
    case 2: {
        ImGui::TextColored(COLOR_PRIMARY, T("\xe7\x8a\xb6\xe6\x80\x81\xe7\x9b\x91\xe6\x8e\xa7", "Status Monitor"));
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe5\xae\x9e\xe6\x97\xb6\xe6\x80\xa7\xe8\x83\xbd\xe5\x92\x8c\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf", "Real-time performance and system info"));
        ImGui::Spacing();

        BeginCard("##card_fps");
        ImGui::Text(T("\xe5\xb8\xa7\xe7\x8e\x87", "Frame Rate"));
        ImGui::Spacing();
        ImGui::TextColored(COLOR_PRIMARY, "%.1f FPS", g_fps);
        static float fps_history[120] = {};
        static int fps_idx = 0;
        fps_history[fps_idx % 120] = g_fps;
        fps_idx++;
        int offset = (fps_idx < 120) ? 0 : (fps_idx % 120);
        ImGui::PlotLines("##fps_plot", fps_history, 120, offset,
            nullptr, 0.0f, 120.0f, ImVec2(-1, 80));
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_sysinfo");
        ImGui::Text(T("\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf", "System Info"));
        ImGui::Spacing();
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87:", "Display:"));
        ImGui::SameLine();
        ImGui::Text("%.0f x %.0f", ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe5\xb8\xa7\xe6\x97\xb6\xe9\x97\xb4:", "Frame Time:"));
        ImGui::SameLine();
        ImGui::Text("%.2f ms", ImGui::GetIO().DeltaTime * 1000.0f);
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_progress");
        ImGui::Text(T("\xe8\xbf\x9b\xe5\xba\xa6", "Progress"));
        ImGui::Spacing();
        static float progress_val = 0.3f;
        g_progress_fill.Set(progress_val);
        float animated_fill = g_progress_fill.Val();
        ImGui::Text("%.0f%%", progress_val * 100.0f);
        ImGui::ProgressBar(animated_fill, ImVec2(-1, 22));
        ImGui::Spacing();
        ImGui::SliderFloat("##progress", &progress_val, 0.0f, 1.0f);
        EndCard();
        break;
    }
    case 3: {
        ImGui::TextColored(COLOR_PRIMARY, T("\xe8\xae\xbe\xe7\xbd\xae", "Settings"));
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe8\x87\xaa\xe5\xae\x9a\xe4\xb9\x89\xe5\xa4\x96\xe8\xa7\x82\xe5\x92\x8c\xe6\x9f\xa5\xe7\x9c\x8b\xe4\xbf\xa1\xe6\x81\xaf", "Customize appearance and view info"));
        ImGui::Spacing();

        // ── 语言设置 ──
        BeginCard("##card_language");
        ImGui::Text(T("\xe8\xaf\xad\xe8\xa8\x80 / Language", "Language"));
        ImGui::Spacing();
        if (ImGui::RadioButton(T("\xe4\xb8\xad\xe6\x96\x87", "Chinese"), g_language == 0)) g_language = 0;
        ImGui::SameLine(0, 20);
        if (ImGui::RadioButton("English", g_language == 1)) g_language = 1;
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_appearance");
        ImGui::Text(T("\xe5\xa4\x96\xe8\xa7\x82", "Appearance"));
        ImGui::Spacing();
        ImGui::Text(T("\xe4\xb8\xbb\xe9\xa2\x9c\xe8\x89\xb2", "Accent Color"));
        ImGui::SameLine(140);
        if (ImGui::ColorEdit3("##accent", (float*)&g_accent_color, ImGuiColorEditFlags_NoInputs)) {
            g_primary_color = g_accent_color;
            ImVec4& c = ImGui::GetStyle().Colors[ImGuiCol_Button];
            c = g_accent_color;
            c = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            c = g_accent_color;
        }
        ImGui::Text(T("\xe4\xb8\xbb\xe8\x89\xb2\xe8\xb0\x83", "Primary Color"));
        ImGui::SameLine(140);
        if (ImGui::ColorEdit3("##primary", (float*)&g_primary_color, ImGuiColorEditFlags_NoInputs)) {
            ImGui::GetStyle().Colors[ImGuiCol_Button] = g_primary_color;
            ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] = ImVec4(g_primary_color.x + 0.05f, g_primary_color.y + 0.06f, g_primary_color.z + 0.05f, 1.0f);
            ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] = ImVec4(g_primary_color.x - 0.04f, g_primary_color.y - 0.06f, g_primary_color.z - 0.05f, 1.0f);
        }
        ImGui::Spacing();
        ImGui::Text(T("\xe5\x9c\x86\xe8\xa7\x92: %.0f px", "Rounding: %.0f px"), g_rounding);
        ImGui::SliderFloat("##rounding", &g_rounding, 0.0f, 16.0f);
        ImGui::GetStyle().WindowRounding  = g_rounding;
        ImGui::GetStyle().ChildRounding   = g_rounding;
        ImGui::GetStyle().FrameRounding   = g_rounding;
        ImGui::GetStyle().PopupRounding   = g_rounding;
        ImGui::GetStyle().ScrollbarRounding = g_rounding;
        ImGui::GetStyle().GrabRounding    = g_rounding;
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_info");
        ImGui::Text(T("\xe4\xbf\xa1\xe6\x81\xaf", "Information"));
        ImGui::Spacing();
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe7\x89\x88\xe6\x9c\xac:", "Version:"));
        ImGui::SameLine();
        ImGui::Text("v1.0.0");
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe6\xa1\x86\xe6\x9e\xb6:", "Framework:"));
        ImGui::SameLine();
        ImGui::Text("ImGui + DirectX 11");
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe4\xb8\xbb\xe9\xa2\x98:", "Theme:"));
        ImGui::SameLine();
        ImGui::TextColored(COLOR_PRIMARY, "Murasame");
        EndCard();

        ImGui::Spacing();
        BeginCard("##card_account");
        ImGui::Text(T("\xe8\xb4\xa6\xe6\x88\xb7", "Account"));
        ImGui::Spacing();
        ImGui::TextColored(COLOR_TEXT_DIM, T("\xe8\xae\xbf\xe9\x97\xae\xe5\xaf\x86\xe9\x92\xa5:", "Access Key:"));
        ImGui::SameLine();
        ImGui::Text("%s", g_correct_key);
        EndCard();
        break;
    }
    }

    ImGui::EndChild();

    ImGui::PopStyleVar();  // 入场动画 Alpha

    ImGui::End();
}

// ====== 角色图片加载 ======
bool LoadCharacterTexture() {
    const wchar_t* imgPath = L"assets/character.png";

    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return false;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(imgPath, nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) { pFactory->Release(); return false; }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) { pDecoder->Release(); pFactory->Release(); return false; }

    pFrame->GetSize((UINT*)&g_charOrigW, (UINT*)&g_charOrigH);

    IWICFormatConverter* pConverter = nullptr;
    hr = pFactory->CreateFormatConverter(&pConverter);
    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        pFrame->Release(); pDecoder->Release(); pFactory->Release(); return false;
    }

    pConverter->QueryInterface(IID_PPV_ARGS(&g_wicOriginal));

    UINT w = g_charOrigW, h = g_charOrigH;
    std::vector<uint8_t> pixels(w * h * 4);
    pConverter->CopyPixels(nullptr, w * 4, w * h * 4, pixels.data());

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = w * 4;

    ID3D11Texture2D* pTex = nullptr;
    g_pd3dDevice->CreateTexture2D(&texDesc, &initData, &pTex);
    if (pTex) {
        g_pd3dDevice->CreateShaderResourceView(pTex, nullptr, &g_charTexSRV);
        pTex->Release();
    }

    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();

    DBG_PRINT("[Character] Texture loaded: %dx%d\n", g_charOrigW, g_charOrigH);
    return true;
}

// ====== 桌面角色叠加窗口 ======
bool CreateCharOverlayBitmap(int targetW, int targetH) {
    if (g_charHBitmap) {
        DeleteObject(g_charHBitmap);
        g_charHBitmap = nullptr;
    }

    if (!g_wicOriginal || targetW <= 0 || targetH <= 0) return false;

    IWICImagingFactory* pFactory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (!pFactory) return false;

    IWICBitmapScaler* pScaler = nullptr;
    HRESULT hr = pFactory->CreateBitmapScaler(&pScaler);
    if (FAILED(hr)) { pFactory->Release(); return false; }

    hr = pScaler->Initialize(g_wicOriginal, targetW, targetH,
        WICBitmapInterpolationModeFant);
    if (FAILED(hr)) { pScaler->Release(); pFactory->Release(); return false; }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = targetW;
    bmi.bmiHeader.biHeight = -targetH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC hdcScreen = GetDC(nullptr);
    g_charHBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (!g_charHBitmap) { pScaler->Release(); pFactory->Release(); return false; }

    hr = pScaler->CopyPixels(nullptr, targetW * 4, targetW * targetH * 4, (BYTE*)pBits);

    pScaler->Release();
    pFactory->Release();

    if (FAILED(hr)) {
        DeleteObject(g_charHBitmap);
        g_charHBitmap = nullptr;
        return false;
    }

    g_charBmpW = targetW;
    g_charBmpH = targetH;
    return true;
}

void CreateCharOverlay(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"CharOverlayWnd";
    RegisterClassExW(&wc);

    // WS_POPUP 无 WS_VISIBLE — 创建时完全不可见，防止闪现黑窗
    g_charOverlayHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
}

void UpdateCharOverlay() {
    if (!g_charOverlayHwnd || !g_wicOriginal) return;
    if (!g_logged_in) {
        // ========== 修改 ==========
        // 未登录时隐藏叠加窗口并重置就绪标志
        if (g_charOverlayReady) {
            ShowWindow(g_charOverlayHwnd, SW_HIDE);
            g_charOverlayReady = false;
        }
        // ========== 修改结束 ==========
        return;
    }

    // ========== 修改：精确计算窗口可见边界，消除 DWM 阴影空隙 ==========
    // 1) 客户区尺寸（用于等高缩放）
    RECT cr;
    GetClientRect(g_hWnd, &cr);
    if (cr.right <= cr.left || cr.bottom <= cr.top) return;

    // 2) 获取主窗口在屏幕上的精确可见边界
    //    优先使用 DWM 扩展边框（排除阴影），回退 GetClientRect+ClientToScreen
    int screenLeft, screenTop;
    RECT ef = {};
    if (SUCCEEDED(DwmGetWindowAttribute(g_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &ef, sizeof(ef)))
        && ef.right > ef.left && ef.bottom > ef.top) {
        // WS_POPUP 无标准边框，DWM 扩展边框 = 窗口可见矩形（含阴影）
        // 再用 GetWindowRect 做差得到阴影宽度
        RECT wr;
        GetWindowRect(g_hWnd, &wr);
        int shadowL = wr.left - ef.left;   // 左侧阴影宽（WS_POPUP 下通常为 0）
        int shadowT = wr.top  - ef.top;    // 顶部阴影高
        screenLeft = wr.left - shadowL;     // 窗口可见左边缘
        screenTop  = wr.top  - shadowT;     // 窗口可见上边缘
    } else {
        // 回退：客户区 (0,0) 转屏幕坐标（WS_POPUP 下 = 窗口左上角）
        POINT clientTL = { 0, 0 };
        ClientToScreen(g_hWnd, &clientTL);
        screenLeft = clientTL.x;
        screenTop  = clientTL.y;
    }

    // 3) 角色叠加窗口尺寸：高度 = 主窗口高度，宽度等比缩放
    float clientH = (float)(cr.bottom - cr.top);
    float scale = clientH / (float)g_charOrigH;
    int newW = (int)(g_charOrigW * scale);
    int newH = (int)clientH;

    if (newW < 1) newW = 1;
    if (newH < 1) newH = 1;

    if (newW != g_charBmpW || newH != g_charBmpH) {
        if (!CreateCharOverlayBitmap(newW, newH)) return;
    }

    // ========== 修复300px巨大间距 ==========
    // 偏移重叠法：不裁剪图片，叠加窗口向右偏移 CHARACTER_OVERLAP_RATIO
    // 重叠部分（透明区域）被主窗口遮挡，可见角色向右移动靠近窗口
    int overlayX = screenLeft - newW + (int)(newW * CHARACTER_OVERLAP_RATIO);
    int overlayY = screenTop;
    // ========== 修复结束 ==========

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, g_charHBitmap);

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    SIZE size = { newW, newH };
    POINT dstPos = { overlayX, overlayY };
    POINT srcPos = { 0, 0 };

    UpdateLayeredWindow(g_charOverlayHwnd, hdcScreen, &dstPos, &size,
        hdcMem, &srcPos, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, oldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // ========== 修改 ==========
    // 只有设置了叠加层内容后才显示窗口，防止空白黑窗闪现
    ShowWindow(g_charOverlayHwnd, SW_SHOWNOACTIVATE);
    g_charOverlayReady = true;
    // ========== 修改结束 ==========

    SetWindowPos(g_charOverlayHwnd, g_hWnd, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DestroyCharOverlay() {
    // ========== 修改 ==========
    g_charOverlayReady = false;
    // ========== 修改结束 ==========
    if (g_charOverlayHwnd) {
        DestroyWindow(g_charOverlayHwnd);
        g_charOverlayHwnd = nullptr;
    }
    if (g_charHBitmap) {
        DeleteObject(g_charHBitmap);
        g_charHBitmap = nullptr;
    }
    if (g_wicOriginal) {
        g_wicOriginal->Release();
        g_wicOriginal = nullptr;
    }
    if (g_charTexSRV) {
        g_charTexSRV->Release();
        g_charTexSRV = nullptr;
    }
}

// ====== D3D 初始化和清理 ======
// ========== 修改 ==========
// 窗口尺寸变化时调整交换链缓冲区（登录卡片 ↔ 主界面切换）
void ResizeSwapChain(HWND hWnd, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    g_pSwapChain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}
// ========== 修改结束 ==========

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 800;
    sd.BufferDesc.Height = 600;
    // ========== 修改 ==========
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // 支持 alpha 通道透明
    // ========== 修改结束 ==========
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
        &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer) != S_OK) {
        g_pSwapChain->Release();
        g_pd3dDevice->Release();
        g_pd3dDeviceContext->Release();
        return false;
    }

    if (g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView) != S_OK) {
        pBackBuffer->Release();
        g_pSwapChain->Release();
        g_pd3dDevice->Release();
        g_pd3dDeviceContext->Release();
        return false;
    }
    pBackBuffer->Release();

    return true;
}

void CleanupDeviceD3D() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// 无边框窗口交互：仅顶部 50px 拖动，禁止调整大小
const int DRAG_BAR_H = 50;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    // ESC 退出程序
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_MOVE:
    case WM_SIZE:
        UpdateCharOverlay();
        break;
    // WM_NCHITTEST: 仅顶部拖动，禁止调整窗口大小
    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT rc;
        GetWindowRect(hWnd, &rc);
        if (pt.y - rc.top < DRAG_BAR_H) return HTCAPTION;
        return HTCLIENT;
    }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ========== 修改 ==========
// WinMain — Windows 应用程序标准入口（无控制台窗口）
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
// ========== 修改结束 ==========
{
    // ========== 修改 ==========
    // DPI 感知：修复系统缩放导致窗口尺寸被压缩的问题
    // 优先 Win10 1703+ SetProcessDpiAwarenessContext(PerMonitorV2)，回退 Vista SetProcessDPIAware
    {
        HMODULE hU32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI* Fn_SetProcessDpiAwarenessContext)(HANDLE);
        auto fn = hU32 ? (Fn_SetProcessDpiAwarenessContext)GetProcAddress(hU32, "SetProcessDpiAwarenessContext") : nullptr;
        if (fn)
            fn((HANDLE)(INT_PTR)-4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        else
            SetProcessDPIAware();
    }
    // ========== 修改结束 ==========

    // ========== 修改 ==========
    // Debug 模式：手动分配控制台用于 printf/cout 调试输出
    // Release 模式：不创建控制台，运行时完全无黑窗口
    #ifdef _DEBUG
    AllocConsole();
    FILE* fDummy = nullptr;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    printf("[Debug] Console allocated for debug output\n");
    DBG_PRINT("[Debug] OutputDebugString test: DBG_PRINT works\n");
    #endif
    // ========== 修改结束 ==========

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ========== 修改 ==========
    // hInstance 已由 WinMain 参数提供，不再需要 GetModuleHandle(nullptr)
    // ========== 修改结束 ==========

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ImGuiApp";
    RegisterClassExW(&wc);

    // ========== 修改 ==========
    // WS_EX_LAYERED + WS_POPUP: 分层透明窗口
    // SetLayeredWindowAttributes(LWA_COLORKEY): 纯黑像素变透明，ImGui 彩色内容保持可见
    {
        int px = (GetSystemMetrics(SM_CXSCREEN) - LOGIN_CARD_W) / 2;
        int py = (GetSystemMetrics(SM_CYSCREEN) - LOGIN_CARD_H) / 2;
        g_hWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW,
            wc.lpszClassName, L"Murasame", WS_POPUP | WS_VISIBLE,
            px, py, LOGIN_CARD_W, LOGIN_CARD_H,
            nullptr, nullptr, hInstance, nullptr);
        // LWA_COLORKEY: 将 RGB(0,0,0) 设为透明色键
        // D3D 清除色为黑色 → 卡片外区域自动透明；ImGui 卡片颜色非纯黑 → 保持可见
        SetLayeredWindowAttributes(g_hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    }
    // ========== 修改结束 ==========

    if (!CreateDeviceD3D(g_hWnd)) {
        CleanupDeviceD3D();
        DestroyWindow(g_hWnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    // ImGui 初始化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImFont* font = nullptr;
    // 多路径尝试：覆盖 VS 调试工作目录、exe 所在目录、exe 上两级目录
    const char* font_paths[] = {
        "../fonts/msyh.ttf",           // VS 调试工作目录 (Project2\Project2\)
        "fonts/msyh.ttf",              // 当前目录
        "../../fonts/msyh.ttf",        // exe 目录 (x64\Release\) 上两级到项目根
        "../../../fonts/msyh.ttf"      // 二级嵌套 exe 目录
    };
    for (const char* path : font_paths) {
        font = io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseFull());
        if (font != nullptr) {
            DBG_PRINT("[Font] Loaded: %s\n", path);
            break;
        }
    }
    if (font == nullptr) {
        io.Fonts->AddFontDefault();
        DBG_PRINT("[Font] Fallback to default font\n");
    }

    ApplyMurasameTheme();
    InitAnimations();  // 动画状态初始化

    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ImGui_ImplDX11_CreateDeviceObjects();  // 创建字体纹理等设备对象

    // 同步 SwapChain 尺寸到登录窗口实际大小，避免坐标偏移
    ResizeSwapChain(g_hWnd, LOGIN_CARD_W, LOGIN_CARD_H);

    LoadCharacterTexture();
    CreateCharOverlay(hInstance);

    DBG_PRINT("[Main] Application started\n");

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                done = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        UpdateAnimations();  // 每帧更新动画

        float delta = io.DeltaTime;
        g_fps_timer += delta;
        g_frame_count++;
        if (g_fps_timer >= 1.0f) {
            g_fps = (float)g_frame_count / g_fps_timer;
            g_fps_timer = 0.0f;
            g_frame_count = 0;
        }

        if (!g_logged_in) {
            DrawLoginPage();
        } else {
            DrawMainUI();
        }

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);

        // ========== 修改 ==========
        // 登录模式：清除为全透明，只有登录卡片可见
        // 主界面模式：清除为丛雨主题背景色
        if (!g_logged_in) {
            float clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };  // 完全透明
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        } else {
            float clear_color[] = { 0.078f, 0.110f, 0.102f, 1.0f };  // 丛雨背景
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        }
        // ========== 修改结束 ==========

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);

        // ========== 修改 ==========
        // 登录状态切换时自动调整窗口尺寸：卡片 500x300 ↔ 主界面 920x620
        if (g_logged_in != g_was_logged_in) {
            g_was_logged_in = g_logged_in;
            if (g_logged_in) {
                int px = (GetSystemMetrics(SM_CXSCREEN) - MAIN_WIN_W) / 2;
                int py = (GetSystemMetrics(SM_CYSCREEN) - MAIN_WIN_H) / 2;
                SetWindowPos(g_hWnd, nullptr, px, py, MAIN_WIN_W, MAIN_WIN_H, SWP_NOZORDER);
                ResizeSwapChain(g_hWnd, MAIN_WIN_W, MAIN_WIN_H);
            } else {
                int px = (GetSystemMetrics(SM_CXSCREEN) - LOGIN_CARD_W) / 2;
                int py = (GetSystemMetrics(SM_CYSCREEN) - LOGIN_CARD_H) / 2;
                SetWindowPos(g_hWnd, nullptr, px, py, LOGIN_CARD_W, LOGIN_CARD_H, SWP_NOZORDER);
                ResizeSwapChain(g_hWnd, LOGIN_CARD_W, LOGIN_CARD_H);
            }
        }
        // ========== 修改结束 ==========

        UpdateCharOverlay();
    }

    DBG_PRINT("[Main] Shutting down\n");

    DestroyCharOverlay();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hWnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    CoUninitialize();

    // ========== 修改 ==========
    // Debug 模式释放控制台
    #ifdef _DEBUG
    FreeConsole();
    #endif
    // ========== 修改结束 ==========

    return 0;
}
