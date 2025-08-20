#include <glad/glad.h>//必须放在GLFW前！！！
#include <GLFW/glfw3.h>

#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "implot_internal.h" 
#include <iostream>
#include <fstream>
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <ThostFtdcMdApi.h>
#include "DataBuffer.h"
#include "Indicator.h"
#include "MdSpi.h"
#include "TdSpi.h"
#include "Strategy.h"
#include <windows.h>
#include <algorithm>
#include<deque>

using namespace std;

struct WindowData {
	GLFWwindow* window;
	ImGuiContext* context;
	std::string title;
	bool open;
};

std::vector<WindowData> windows;

struct PendingWindow {
	std::string title;
	int width;
	int height;
};

deque<PendingWindow> pendingWindows;

// 蜡烛图数据：按合约存储
map<string, vector<CandleData>> candlesMap;
// 当前选中的蜡烛图合约
string g_selectedCandleSymbol;

// 为每个窗口设置 ImGui 上下文和 OpenGL 上下文
void SetupImGuiForWindow(GLFWwindow* window, const char* glsl_version, ImGuiContext*& outContext) {
	outContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(outContext);
	ImGui::StyleColorsDark();

	//创建并绑定 ImPlot 上下文
	ImPlot::CreateContext();
	ImPlot::SetCurrentContext(ImPlot::GetCurrentContext());

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\simhei.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);
}


// 将索引转换为时间字符串（根据当前选中的合约）

const char* IndexToTime(double index, void*) {
	extern map<string, vector<CandleData>> candlesMap;
	extern string g_selectedCandleSymbol;
	auto it = candlesMap.find(g_selectedCandleSymbol);
	if (it == candlesMap.end()) return "";
	const auto& candles = it->second;
	int idx = static_cast<int>(index);
	if (idx >= 0 && idx < (int)candles.size()) {
		return candles[idx].time.c_str();
	}
	return "";
}


//candlechart
void DrawCandlestickChart(const std::vector<CandleData>& candles) {
	if (ImPlot::BeginPlot("Candlestick Chart")) {

		// 生成索引数组
		std::vector<double> xs(candles.size());
		std::vector<const char*> labels(candles.size());
		for (int i = 0; i < (int)candles.size(); i++) {
			xs[i] = (double)i; // x坐标用索引
			labels[i] = candles[i].time.c_str(); // label用时间字符串
		}

		// 扫描 Y 轴范围
		double minPrice = candles[0].low;
		double maxPrice = candles[0].high;
		for (const auto& c : candles) {
			if (c.low < minPrice) minPrice = c.low;
			if (c.high > maxPrice) maxPrice = c.high;
		}
		double padding = (maxPrice - minPrice) * 0.1; // 上下各留10%
		if (padding <= 0) padding = 1.0;              // 防止区间为0
		double yMin = minPrice - padding;
		double yMax = maxPrice + padding;

		// 设置 X 轴刻度为时间字符串
		ImPlot::SetupAxes("Time", "Price");
		int N = 50;
		if (candles.size() > 0) {
			double max_x = (double)candles.size();
			double min_x = max_x > N ? max_x - N : 0;
			ImPlot::SetupAxisLimits(ImAxis_X1, min_x, max_x, ImGuiCond_Once);
		}
		ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
		ImPlot::SetupAxisTicks(ImAxis_X1, xs.data(), (int)xs.size(), labels.data());


		// 绘制蜡烛图
		ImDrawList* draw_list = ImPlot::GetPlotDrawList();
		float half_width = 0.4f;

		for (int i = 0; i < candles.size(); i++) {
			const auto& c = candles[i];

			// 上下影线
			ImVec2 p1 = ImPlot::PlotToPixels((double)i, c.low);
			ImVec2 p2 = ImPlot::PlotToPixels((double)i, c.high);
			draw_list->AddLine(p1, p2, IM_COL32(255, 255, 255, 255));

			// 实体
			ImVec2 box1 = ImPlot::PlotToPixels((double)i - half_width, c.open);
			ImVec2 box2 = ImPlot::PlotToPixels((double)i + half_width, c.close);

			ImU32 color = (c.close > c.open) ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
			draw_list->AddRectFilled(box1, box2, color);
		}

		// 十字光标与悬浮信息
		if (ImPlot::IsPlotHovered()) {
			ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
			ImPlotRect limits = ImPlot::GetPlotLimits();
			// 限制X在范围内
			double clamped_x = mouse_pos.x;
			if (clamped_x < limits.X.Min) clamped_x = limits.X.Min;
			if (clamped_x > limits.X.Max) clamped_x = limits.X.Max;
			// 垂直线
			ImVec2 v1 = ImPlot::PlotToPixels(clamped_x, limits.Y.Min);
			ImVec2 v2 = ImPlot::PlotToPixels(clamped_x, limits.Y.Max);
			draw_list->AddLine(v1, v2, IM_COL32(200, 200, 200, 160));
			// 水平线
			ImVec2 h1 = ImPlot::PlotToPixels(limits.X.Min, mouse_pos.y);
			ImVec2 h2 = ImPlot::PlotToPixels(limits.X.Max, mouse_pos.y);
			draw_list->AddLine(h1, h2, IM_COL32(200, 200, 200, 160));

			// 选中最近K线索引
			int nearest = (int)llround(clamped_x);
			nearest = nearest < 0 ? 0 : (nearest >= (int)candles.size() ? (int)candles.size() - 1 : nearest);
			const auto& c = candles[nearest];

			// 高亮选中K线边框
			ImVec2 hb1 = ImPlot::PlotToPixels((double)nearest - half_width, c.open);
			ImVec2 hb2 = ImPlot::PlotToPixels((double)nearest + half_width, c.close);
			draw_list->AddRect(hb1, hb2, IM_COL32(255, 255, 0, 220), 0.0f, 0, 2.0f);

			// 悬浮提示
			ImGui::BeginTooltip();
			ImGui::Text("%s", c.time.c_str());
			ImGui::Separator();
			ImGui::Text("O: %.3f", c.open);
			ImGui::Text("H: %.3f", c.high);
			ImGui::Text("L: %.3f", c.low);
			ImGui::Text("C: %.3f", c.close);
			ImGui::EndTooltip();
		}

		ImPlot::EndPlot();
	}
}

// 渲染每个窗口

map<string, string> accountConfig_map;//保存账户信息的map
std::map<std::string, std::string> contracts_map;//保存合约信息的表
map<string, CThostFtdcInstrumentField> g_mapInstruments; //保存所订阅的合约信息
vector<pair<string, string>> g_vctIFSpreads; //做套利用到两个合约的名称
//char** ppInstrumentID; //保存需要订阅的合约

string g_strLeg1 = "sc2510";
string g_strLeg2 = "sc2511";

void ReadConfigMap(map<string, string>& accountmap);
void ReadContracts(map<std::string, std::string>& contractmap);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);// 回调函数，用来重新设置 OpenGL 视口大小
double ConvertToUnixTime(uint64_t dt);
void RenderWindow(WindowData& wd, const char* glsl_version);

CThostFtdcTraderApi* pTDUserApi;
CThostFtdcMdApi* pMDUserApi;
CSimpleStrategy* g_pSimpleStrategy;
CDataBuffer* g_pDataBuffer;
CIndicator* g_pCIndicator;
//CThostFtdcTraderApi* pTDUserApi = nullptr;


char AUTHCODE[] = "0000000000000000";
char APPID[] = "simnow_client_test";
TThostFtdcBrokerIDType	BROKER_ID;
TThostFtdcInvestorIDType INVESTOR_ID;
TThostFtdcPasswordType	PASSWORD;

//const char *ppInstrumentID[]={"IF2412","IF2501"};
int iInstrumentID;
int iRequestID; // 请求编号
vector<string> g_vctIFCodes;


int main()
{
	cerr << "---------------------------------------------" << endl;
	cerr << "---------------------------------------------" << endl;
	cerr << "-------------CTP高频交易系统启动-------------" << endl;
	cerr << "---------------------------------------------" << endl;
	cerr << "---------------------------------------------" << endl;
	//-----------------1、读取账户信息和订阅的合约信息-------------------
	ReadConfigMap(accountConfig_map);
	ReadContracts(contracts_map);
	strcpy_s(BROKER_ID, accountConfig_map["brokerId"].c_str());
	strcpy_s(INVESTOR_ID, accountConfig_map["userId"].c_str());
	strcpy_s(PASSWORD, accountConfig_map["passwd"].c_str());
	strcpy_s(AUTHCODE, accountConfig_map["authcode"].c_str());
	strcpy_s(APPID, accountConfig_map["appid"].c_str());

	//-----------------2、创建行情Api和回调类实例------------------------
	//-----------------参考ctp函数手册中的行情/交易接口API---------------
	CThostFtdcMdApi* pMDUserApi = CThostFtdcMdApi::CreateFtdcMdApi("./ctpTmp/MarketFlow/");
	const char* ver_Md = CThostFtdcMdApi::GetApiVersion();
	printf("行情API版本：%s\n", ver_Md);
	MdSpi* pMDUserSpi = new MdSpi(pMDUserApi);
	pMDUserApi->RegisterSpi(pMDUserSpi);

	//-----------------3、创建交易Api和回调类实例------------------------
	pTDUserApi = CThostFtdcTraderApi::CreateFtdcTraderApi("./ctpTmp/TradeFlow/");
	TdSpi* pTDUserSpi = new TdSpi();
	pTDUserApi->RegisterSpi(pTDUserSpi);//api注册回调类

	pTDUserApi->SubscribePublicTopic(THOST_TERT_RESTART);//订阅公有流
	pTDUserApi->SubscribePrivateTopic(THOST_TERT_QUICK);//订阅私有流

	//恢复策略类指针初始化，并设置策略的初始状态，即开仓条件
	g_pDataBuffer = new CDataBuffer();
	g_pCIndicator = new CIndicator();
	g_pSimpleStrategy = new CSimpleStrategy(g_pDataBuffer, g_strLeg1, g_strLeg2, pTDUserApi,BROKER_ID, INVESTOR_ID);
	g_pSimpleStrategy->SetCondition(100, 50, 50, 50); //自己根据看盘调整开仓平仓的阈值,可以用indicator填充数值




	//--------------启动行情线程，行情线程引导交易线程-
	char mdFront[50];
	strcpy_s(mdFront, sizeof(mdFront), accountConfig_map["MarketFront"].c_str());
	pMDUserApi->RegisterFront(mdFront);
	pMDUserApi->Init();

	//------------可视化---------------------
// 初始化 GLFW
	if (!glfwInit()) {
		std::cerr << "GLFW 初始化失败\n";
		return -1;
	}
	const char* glsl_version = "#version 460";//new
	// 设置 OpenGL 版本和属性
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// 创建窗口
	/*GLFWwindow* window = glfwCreateWindow(800, 600, "ImGui + GLFW + GLAD", nullptr, nullptr);
	if (!window) {
		std::cerr << "窗口创建失败\n";
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSwapInterval(1); // 开启 vsync

	*/
	GLFWwindow* mainWin = glfwCreateWindow(800, 600, u8"主窗口", nullptr, nullptr);
	glfwMakeContextCurrent(mainWin);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	ImGuiContext* mainCtx = nullptr;
	SetupImGuiForWindow(mainWin, glsl_version, mainCtx);
	windows.push_back({ mainWin, mainCtx, u8"主窗口", true });
/*	// 初始化 GLAD
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cerr << "GLAD 初始化失败\n";
		return -1;
	}

	// 初始化 ImGui，获取 ImGui 的 I/O 控制对象
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// 加载系统中文字体（默认路径）
	const char* font_path = "C:/Windows/Fonts/msyh.ttc"; // 微软雅黑字体
	ImFont* font_cn = io.Fonts->AddFontFromFileTTF(
		font_path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

	if (font_cn == nullptr) {
		std::cerr << "中文字体加载失败，请确认字体路径是否存在：" << font_path << std::endl;
	}

	// 设置 UI 风格：暗色
	ImGui::StyleColorsDark();

	// 绑定平台和渲染器
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 460");

*/
	// 主循环

	//主循环用到的变量
	int selectedSpreadIdx = 0; // 默认选中第一个合约对
	static int meanWindowInput = 30; // 输入框的值，初始为30
	static int meanWindow = 30;      // 实际生效的值

	static std::vector<double> spreadLLHistory; // 保存价差历史
	static std::vector<double> timeHistory;     // 保存时间戳历史
	static int maxHistory = 200; // 最多显示200个点
	static double lastPlotTime = 0.0;
/*
	while (!glfwWindowShouldClose(window)) {

		glfwPollEvents();

		// 开始新帧
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();


		// 获取当前主窗口帧缓冲尺寸
		int display_Wide, display_Height;
		glfwGetFramebufferSize(window, &display_Wide, &display_Height);

		// 设置宽高比
		float aspect_ratio = 4.0f / 3.0f;

		// 左半边宽度
		float half_width = display_Wide * 0.5f;
		float height = half_width / aspect_ratio;

		// 保证不超出窗口高度（等比例缩放限制）
		if (height > display_Height)
			height = (float)display_Height;

		// 设置窗口位置和大小（始终靠左、始终等比）
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(half_width, height), ImGuiCond_Always);

		// 示例窗口
		ImGui::Begin(u8"交易数据可视化");
		ImGui::Text(u8"套利合约1: %s", g_strLeg1.c_str());
		ImGui::Text(u8"套利合约2: %s", g_strLeg2.c_str());
		std::vector<std::string> comboItems;
		for (int i = 0; i < g_vctIFSpreads.size(); ++i) {
			char buf[128];
			snprintf(buf, sizeof(buf), u8"%d %s-%s", i + 1, g_vctIFSpreads[i].first.c_str(), g_vctIFSpreads[i].second.c_str());
			comboItems.push_back(buf);
		}
		std::vector<const char*> comboItemsCStr;
		for (const auto& s : comboItems) comboItemsCStr.push_back(s.c_str());
		if (!comboItemsCStr.empty()) {
			ImGui::Combo(u8"合约对", &selectedSpreadIdx, comboItemsCStr.data(), comboItemsCStr.size(),3);
		}
		else {
			ImGui::Text(u8"暂无合约对");
		}

		// 滑动均值窗口参数调整
		ImGui::InputInt(u8"此处修改参数", &meanWindowInput);
		if (ImGui::Button(u8"确定")) {
			if (meanWindowInput > 0) {
				meanWindow = meanWindowInput;
				if (g_pSimpleStrategy) {
					g_pSimpleStrategy->SetMeanWindow(meanWindow); // 在strategy里实现这个接口
				}
			}
		}
		ImGui::SameLine();
		ImGui::Text(u8"当前indicator中的参数为: %d", meanWindow);
		ImGui::End();

		
		//画图// 折线图窗口
		ImGui::Begin(u8"价差折线图");
		if (!g_vctIFSpreads.empty()) {
			auto& spreadPair = g_vctIFSpreads[selectedSpreadIdx];
			std::vector<CThostFtdcDepthMarketDataField> vctLeg1, vctLeg2;
			std::vector<double> xTime, ySpreadLL;
			if (g_pDataBuffer && g_pDataBuffer->GetSeries(spreadPair.first, vctLeg2) && g_pDataBuffer->GetSeries(spreadPair.second, vctLeg1)) {
				size_t minLen = min(vctLeg1.size(), vctLeg2.size());
				for (size_t i = 0; i < minLen; ++i) {
					uint64_t rawTime = CIndicator::GetDateTime(vctLeg1[i]);
					double t = ConvertToUnixTime(rawTime);
					double price1 = vctLeg1[i].LastPrice;
					double price2 = vctLeg2[i].LastPrice;
					if (std::isfinite(t) && std::isfinite(price1) && std::isfinite(price2)) {
						xTime.push_back(t);
						ySpreadLL.push_back(price1 - price2);
					}
				}
			}
			if (!xTime.empty() && xTime.size() == ySpreadLL.size()) {
				if (ImPlot::BeginPlot(u8"LL价差", ImVec2(-1, 300))) {
					ImPlot::SetupAxes(u8"时间", u8"LL价差", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
					ImPlot::SetupAxisFormat(ImAxis_X1, "%H:%M:%S");
					ImPlot::PlotLine(u8"LL价差", xTime.data(), ySpreadLL.data(), (int)xTime.size());
					ImPlot::EndPlot();
				}
			}
			else {
				ImGui::Text(u8"无足够数据可画图");
			}
		}
		else {
			ImGui::Text(u8"无合约对可画图");
			// 画一个sin(x)函数（一个周期）
			std::vector<double> xSin, ySin;
			const int N = 100;
			for (int i = 0; i < N; ++i) {
				double x = i * 2 * 3.14 / (N - 1);
				xSin.push_back(x);
				ySin.push_back(std::sin(x));
				
			}
			if (ImPlot::BeginPlot("sin(x)", ImVec2(-1, 300))) {
				ImPlot::SetupAxes("x", "sin(x)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
				ImPlot::PlotLine("sin(x)", xSin.data(), ySin.data(), N);
				ImPlot::EndPlot();
				
			}
		}
		ImGui::End();



		// 渲染
		ImGui::Render();//生成画图命令
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());//生成图元传到GPU，然后生成像素

		glfwSwapBuffers(window);
	}

	// 清理
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
*/


while (!windows.empty()) {
	glfwPollEvents();

	// ✅ 处理待创建窗口
	while (!pendingWindows.empty()) {
		auto& p = pendingWindows.front();
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		GLFWwindow* newWin = glfwCreateWindow(p.width, p.height, p.title.c_str(), nullptr, nullptr);
		if (!newWin) {
			std::cerr << "无法创建窗口: " << p.title << std::endl;
			pendingWindows.pop_front();
			continue;
		}

		glfwMakeContextCurrent(newWin);
		gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

		ImGuiContext* newCtx = nullptr;
		SetupImGuiForWindow(newWin, glsl_version, newCtx);
		glfwShowWindow(newWin);
		windows.push_back({ newWin, newCtx, p.title, true });

		pendingWindows.pop_front();
	}

	// 设置当前上下文为活跃窗口
	for (auto& wd : windows) {
		if (glfwGetWindowAttrib(wd.window, GLFW_FOCUSED)) {
			glfwMakeContextCurrent(wd.window);
			ImGui::SetCurrentContext(wd.context);
			break;
		}
	}

	// 渲染所有窗口
	for (auto it = windows.begin(); it != windows.end();) {
		if (glfwWindowShouldClose(it->window) || !it->open) {
			ImGui::SetCurrentContext(it->context);
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			glfwDestroyWindow(it->window);
			it = windows.erase(it);
		}
		else {
			RenderWindow(*it, glsl_version);
			++it;
		}
	}
}

    glfwTerminate();
	//--------------阻塞行情与交易线程-----------------
	pMDUserApi->Join();
	pTDUserApi->Join();
	pMDUserApi->Release();
	pTDUserApi->Release();
	return 0;
}



void ReadContracts(map<std::string, std::string>& contractmap)
{
	std::ifstream file2(".\\x64\\Debug\\contracts.txt", ios::in);
	string fieldKey;
	string fieldValue;
	char dataLine[256];
	if (!file2)
	{
		cout << "配置文件不存在" << endl;
		return;
	}
	else
	{
		while (file2.getline(dataLine, sizeof(dataLine), '\n'))
		{
			int length = strlen(dataLine);
			char tmp[128];
			for (int i = 0, j = 0, count = 0; i < length + 1; i++)
			{
				if (dataLine[i] != ',' && dataLine[i] != '\0')
					tmp[j++] = dataLine[i];
				else
				{

					tmp[j] = '\0';
					count++;
					j = 0;
					switch (count)
					{
					case 1:
						fieldKey = tmp;
						break;
					case 2:
						fieldValue = tmp;
					default:
						break;
					}
				}
			}
			contractmap.insert(make_pair(fieldKey, fieldValue));
		}
	}
	file2.close();
}

void ReadConfigMap(map<std::string, std::string>& accountmap)
{
	std::ifstream file1(".\\x64\\Debug\\config.txt", ios::in);
	string fieldKey;
	string fieldValue;
	char dataLine[256];
	if (!file1)
	{
		cout << "配置文件不存在" << endl;
		return;
	}
	else
	{
		while (file1.getline(dataLine, sizeof(dataLine), '\n'))
		{
			int length = strlen(dataLine);
			char tmp[128];
			for (int i = 0, j = 0, count = 0; i < length + 1; i++)
			{
				if (dataLine[i] != ',' && dataLine[i] != '\0')
					tmp[j++] = dataLine[i];
				else
				{
					tmp[j] = '\0';
					count++;
					j = 0;
					switch (count)
					{
					case 1:
						fieldKey = tmp;
						break;
					case 2:
						fieldValue = tmp;
					default:
						break;
					}
				}
			}
			accountConfig_map.insert(make_pair(fieldKey, fieldValue));
		}
	}
	file1.close();
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) // 回调函数，用来重新设置 OpenGL 视口大小
{
	glViewport(0, 0, width, height);
}

double ConvertToUnixTime(uint64_t dt) {
	uint64_t ms = dt % 1000;
	dt /= 1000;
	uint64_t sec = dt % 100;
	dt /= 100;
	uint64_t min = dt % 100;
	dt /= 100;
	uint64_t hour = dt % 100;
	dt /= 100;
	uint64_t day = dt % 100;
	dt /= 100;
	uint64_t month = dt % 100;
	dt /= 100;
	uint64_t year = dt;
	struct tm t = {};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = min;
	t.tm_sec = sec;
	time_t unix_ts = mktime(&t);
	return unix_ts + ms / 1000.0;
}

void RenderWindow(WindowData& wd, const char* glsl_version) {
	glfwMakeContextCurrent(wd.window);
	ImGui::SetCurrentContext(wd.context);
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	int selectedSpreadIdx = 0;
	static int meanWindowInput = 30; // 输入框的值，初始为30
	static int meanWindow = 30;      // 实际生效的值
	static std::vector<double> spreadLLHistory; // 保存价差历史
	static std::vector<double> timeHistory;     // 保存时间戳历史
	static int maxHistory = 200; // 最多显示200个点
	static double lastPlotTime = 0.0;

	if (ImGui::Begin(wd.title.c_str(), &wd.open, ImGuiWindowFlags_MenuBar)) {
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu(u8"合约菜单")) {
				if (ImGui::MenuItem(u8"新建合约菜单")) {
					pendingWindows.push_back({ u8"新合约 " + std::to_string(windows.size()), 800, 600 });
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		static float f = 0.0f;
		static char buffer[128] = u8"交易数据可视化";
		static int selectedSpreadIdx = 0;
		ImGui::InputText(u8"输入框", buffer, IM_ARRAYSIZE(buffer));
		ImGui::SliderFloat(u8"滑条", &f, 0.0f, 1.0f);
		if (ImGui::Button(u8"按钮")) {
			std::cout << u8"点击了按钮: " << wd.title << std::endl;
		}
		ImGui::Text(u8"套利合约1: %s", g_strLeg1.c_str());
		ImGui::Text(u8"套利合约2: %s", g_strLeg2.c_str());
		vector<std::string> comboItems;
		for (int i = 0; i < g_vctIFSpreads.size(); ++i) {
			char buf[128];
			snprintf(buf, sizeof(buf), u8"%d %s-%s", i + 1, g_vctIFSpreads[i].first.c_str(), g_vctIFSpreads[i].second.c_str());
			comboItems.push_back(buf);
		}
		vector<const char*> comboItemsCStr;
		for (const auto& s : comboItems) comboItemsCStr.push_back(s.c_str());
		if (!comboItemsCStr.empty()) {
			ImGui::Combo(u8"合约对", &selectedSpreadIdx, comboItemsCStr.data(), comboItemsCStr.size(), 3);
		}
		else {
			ImGui::Text(u8"暂无合约对");
		}

		// 滑动均值窗口参数调整
		ImGui::InputInt(u8"此处修改参数", &meanWindowInput);
		if (ImGui::Button(u8"确定")) {
			if (meanWindowInput > 0) {
				meanWindow = meanWindowInput;
				if (g_pSimpleStrategy) {
					g_pSimpleStrategy->SetMeanWindow(meanWindow); // 在strategy里实现这个接口
				}
			}
		}
		ImGui::SameLine();
		ImGui::Text(u8"当前indicator中的参数为: %d", meanWindow);
	}
	ImGui::End();

	// 蜡烛图选择窗口
	ImGui::Begin(u8"蜡烛图选择", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	static int selectedContractIdx = 0; // 0: g_strLeg1, 1: g_strLeg2
	const char* contractOptions[] = { g_strLeg1.c_str(), g_strLeg2.c_str() };
	ImGui::Combo(u8"选择合约", &selectedContractIdx, contractOptions, 2);
	g_selectedCandleSymbol = (selectedContractIdx == 0) ? g_strLeg1 : g_strLeg2;

	// 显示当前选中的合约信息
	ImGui::Text(u8"当前选中合约: %s", g_selectedCandleSymbol.c_str());

	// 显示蜡烛图数据统计
	{
		auto it = candlesMap.find(g_selectedCandleSymbol);
		if (it != candlesMap.end() && !it->second.empty()) {
			const auto& candles = it->second;
			ImGui::Text(u8"蜡烛图数据数量: %zu", candles.size());
			ImGui::Text(u8"最新数据 - 时间: %s, 开盘: %.3f, 最高: %.3f, 最低: %.3f, 收盘: %.3f",
				candles.back().time.c_str(), candles.back().open, candles.back().high, candles.back().low, candles.back().close);
		}
		else {
			ImGui::Text(u8"暂无蜡烛图数据，等待行情数据...");
		}
	}
	ImGui::End();

	// 蜡烛图绘制窗口
	ImGui::Begin(u8"蜡烛图", nullptr);
	{
		auto it = candlesMap.find(g_selectedCandleSymbol);
		if (it != candlesMap.end() && !it->second.empty()) {
			DrawCandlestickChart(it->second);
		}
		else {
			ImGui::Text(u8"暂无数据可绘制");
		}
	}
	ImGui::End();

	ImGui::Render();
	int display_w, display_h;
	glfwGetFramebufferSize(wd.window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(wd.window);
}