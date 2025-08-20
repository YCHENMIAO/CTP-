#pragma once
#include <cstdint>
#include "ThostFtdcUserApiStruct.h"
#include "DataBuffer.h"
using namespace std;

typedef struct tagValues
{
	uint64_t qwDataTime;
	double dValue;
}Values;
class CIndicator
{
public:
	CIndicator();
	~CIndicator();

public:
	static uint64_t GetDateTime(CThostFtdcDepthMarketDataField stTick); //?????
	// 收集30个tick的均值
	static bool CollectMeanPrice(CDataBuffer* pBuffer, const string& strInstrumentID, double& dMean,int len);
	// 计算滑动均值序列
	static vector<Values> LegMean(vector<CThostFtdcDepthMarketDataField> vctickSeriesBuffer, int Len);
	//计算滑动的标准差σ
	vector<Values> LegStd(vector<CThostFtdcDepthMarketDataField> vctickSeriesBuffer, int Len);
};