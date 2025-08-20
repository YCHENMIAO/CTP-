#include "Indicator.h"
#include <cstring>
#include <cstdlib> 


CIndicator::CIndicator()
{
}

CIndicator::~CIndicator()
{
}

uint64_t CIndicator::GetDateTime(CThostFtdcDepthMarketDataField stTick)
{
	uint64_t qwTime;
	uint64_t dwDate;
	uint64_t dwTime;
	dwDate = atoi(stTick.TradingDay);
	char szTime[7];
	memset(szTime, 0, 7);
	szTime[0] = stTick.UpdateTime[0];
	szTime[1] = stTick.UpdateTime[1];
	szTime[2] = stTick.UpdateTime[3];
	szTime[3] = stTick.UpdateTime[4];
	szTime[4] = stTick.UpdateTime[6];
	szTime[5] = stTick.UpdateTime[7];
	dwTime = atoi(szTime);
	qwTime = dwDate * 1000000000 + dwTime * 1000 + stTick.UpdateMillisec;
	return qwTime;
}

bool CIndicator::CollectMeanPrice(CDataBuffer* pBuffer, const string& strInstrumentID, double& dMean,int len)
{
	vector<CThostFtdcDepthMarketDataField> vctSeries;
	if (!pBuffer->GetSeries(strInstrumentID, vctSeries) || vctSeries.size() < len)
		return false;
	double sum = 0.0;
	// 取最后30个tick
	for (size_t i = vctSeries.size() - len; i < vctSeries.size(); ++i)
	{
		sum += vctSeries[i].LastPrice;
	}
	dMean = sum / len;
	return true;
}

vector<Values> CIndicator::LegMean(vector<CThostFtdcDepthMarketDataField> vctickSeriesBuffer, int Len)
{
	vector<Values> result;
	if (vctickSeriesBuffer.size() < (size_t)Len || Len <= 0)
		return result;
	for (size_t i = Len - 1; i < vctickSeriesBuffer.size(); ++i)
	{
		double sum = 0;
		for (size_t j = i - Len + 1; j <= i; ++j)
		{
			sum += vctickSeriesBuffer[j].LastPrice;
		}
		Values val;
		val.qwDataTime = GetDateTime(vctickSeriesBuffer[i]);
		val.dValue = sum / Len;
		result.push_back(val);
	}
	return result;
}

vector<Values> CIndicator::LegStd(vector<CThostFtdcDepthMarketDataField> vctickSeriesBuffer, int Len)
{
	vector<Values> result;
	if (vctickSeriesBuffer.size() < (size_t)Len || Len <= 0)
		return result;
	for (size_t i = Len - 1; i < vctickSeriesBuffer.size(); ++i)
	{
		double sum = 0;
		double sumsq = 0;
		for (size_t j = i - Len + 1; j <= i; ++j)
		{
			double price = vctickSeriesBuffer[j].LastPrice;
			sum += price;
			sumsq += price * price;
		}
		double mean = sum / Len;
		double var = (sumsq / Len) - (mean * mean);
		double std = var > 0 ? sqrt(var) : 0;
		Values val;
		val.qwDataTime = GetDateTime(vctickSeriesBuffer[i]);
		val.dValue = std;
		result.push_back(val);
	}
	return result;
}