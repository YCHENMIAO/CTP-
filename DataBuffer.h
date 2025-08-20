#pragma once
#include "ThostFtdcUserApiStruct.h"
#include <map>
#include <string>
#include <vector>
#include <windows.h>

using namespace std;


typedef struct tagBufferInfo
{
	CThostFtdcDepthMarketDataField stLastTick;
	vector<CThostFtdcDepthMarketDataField> vctTickSeries;
}BufferInfo;

class CDataBuffer
{
public:
	CDataBuffer();
	~CDataBuffer();
protected:
	map<string, BufferInfo> m_mapBuffer;
	CRITICAL_SECTION m_lockBuffer;
public:
	void PushNewTick(CThostFtdcDepthMarketDataField stTick);
	bool GetTick(string strCode, CThostFtdcDepthMarketDataField& stTick);
	bool GetSpread(string strCodeA, string strCodeB, double& dSpreadLL, double& dSpreadBB, double& dSpreadBA, double& dSpreadAB, double& dSpreadAA);
	bool GetSeries(string strCode, vector<CThostFtdcDepthMarketDataField>& vctSeries);
	//画candletick
	bool CollectMaxMinAndLastInfo(const string& strCode, int nTickCount, double& dMax, double& dMin, string& lastInstrumentID, string& lastUpdateTime, double& lastPrice);

};

//用来画candle的结构体
struct CandleData {
	string time;   // 真实时间字符串，例如 "18:30"
	double open;
	double high;
	double low;
	double close;
};
extern map<string, vector<CandleData>> candlesMap;//放蜡烛图的map