#include "DataBuffer.h"
//?/
CDataBuffer::CDataBuffer()
{
	InitializeCriticalSection(&m_lockBuffer);
}

CDataBuffer::~CDataBuffer()
{
	DeleteCriticalSection(&m_lockBuffer);
}

void CDataBuffer::PushNewTick(CThostFtdcDepthMarketDataField stTick)
{
	string strInstrumentID;
	strInstrumentID = stTick.InstrumentID;

	map<string, BufferInfo>::iterator iter;
	EnterCriticalSection(&m_lockBuffer);
	iter = m_mapBuffer.find(strInstrumentID);
	if (iter != m_mapBuffer.end())//有key，则用新tick覆盖stLastTick（最新tick），并把tick追加到tick序列（vctTickSeries）中
	{
		memcpy(&iter->second.stLastTick, &stTick, sizeof(CThostFtdcDepthMarketDataField));
		iter->second.vctTickSeries.push_back(stTick);
	}
	else//没key则，插入map
	{
		BufferInfo stBufferInfo;
		memcpy(&stBufferInfo.stLastTick, &stTick, sizeof(CThostFtdcDepthMarketDataField));
		stBufferInfo.vctTickSeries.push_back(stTick);
		m_mapBuffer.insert(make_pair(strInstrumentID, stBufferInfo));
	}
	LeaveCriticalSection(&m_lockBuffer);
}

bool CDataBuffer::GetTick(string strCode, CThostFtdcDepthMarketDataField& stTick)//找到并更新lasttick，找到返回True
{
	bool bSuccess = false;
	map<string, BufferInfo>::iterator iter;
	EnterCriticalSection(&m_lockBuffer);
	iter = m_mapBuffer.find(strCode);
	if (iter != m_mapBuffer.end())
	{
		memcpy(&stTick, &iter->second.stLastTick, sizeof(CThostFtdcDepthMarketDataField));
		bSuccess = true;
	}
	LeaveCriticalSection(&m_lockBuffer);
	return bSuccess;
}

bool CDataBuffer::GetSeries(string strCode, vector<CThostFtdcDepthMarketDataField>& vctSeries)//找到tick序列传给参数，找到返回True
{
	bool bSuccess = false;
	map<string, BufferInfo>::iterator iter;
	EnterCriticalSection(&m_lockBuffer);
	iter = m_mapBuffer.find(strCode);
	if (iter != m_mapBuffer.end())
	{
		vctSeries = iter->second.vctTickSeries;
		bSuccess = true;
	}
	LeaveCriticalSection(&m_lockBuffer);
	return bSuccess;
}

bool CDataBuffer::GetSpread(string strCodeA, string strCodeB, double& dSpreadLL, double& dSpreadBB, double& dSpreadBA, double& dSpreadAB, double& dSpreadAA)
{
	bool bSuccess = false;
	map<string, BufferInfo>::iterator iterA, iterB;
	EnterCriticalSection(&m_lockBuffer);
	iterA = m_mapBuffer.find(strCodeA);
	iterB = m_mapBuffer.find(strCodeB);
	if (iterA != m_mapBuffer.end() && iterB != m_mapBuffer.end())
	{
		dSpreadLL = iterA->second.stLastTick.LastPrice - iterB->second.stLastTick.LastPrice;
		dSpreadBB = iterA->second.stLastTick.BidPrice1 - iterB->second.stLastTick.BidPrice1;
		dSpreadBA = iterA->second.stLastTick.BidPrice1 - iterB->second.stLastTick.AskPrice1;
		dSpreadAB = iterA->second.stLastTick.AskPrice1 - iterB->second.stLastTick.BidPrice1;
		dSpreadAA = iterA->second.stLastTick.AskPrice1 - iterB->second.stLastTick.AskPrice1;
		bSuccess = true;
	}
	LeaveCriticalSection(&m_lockBuffer);
	return bSuccess;
}//找到并更新价差给参数，成功返回True

bool CDataBuffer::CollectMaxMinAndLastInfo(const string& strCode, int nTickCount, double& dMax, double& dMin, string& lastInstrumentID, string& lastUpdateTime, double& lastPrice)
{
	bool bSuccess = false;
	EnterCriticalSection(&m_lockBuffer);
	auto iter = m_mapBuffer.find(strCode);
	if (iter != m_mapBuffer.end() && !iter->second.vctTickSeries.empty())
	{
		const auto& vct = iter->second.vctTickSeries;
		int n = min(nTickCount, (int)vct.size());
		if (n > 0)
		{
			dMax = vct[vct.size() - n].LastPrice;
			dMin = vct[vct.size() - n].LastPrice;
			for (int i = vct.size() - n; i < vct.size(); ++i)
			{
				if (vct[i].LastPrice > dMax) dMax = vct[i].LastPrice;
				if (vct[i].LastPrice < dMin) dMin = vct[i].LastPrice;
			}
			// 收集最后一个Tick的信息
			const auto& lastTick = vct.back();
			lastInstrumentID = lastTick.InstrumentID;
			lastUpdateTime = lastTick.UpdateTime;
			lastPrice = lastTick.LastPrice;
			bSuccess = true;
		}
	}
	LeaveCriticalSection(&m_lockBuffer);
	return bSuccess;
}