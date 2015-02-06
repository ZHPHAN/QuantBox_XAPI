#include "stdafx.h"
#include "TraderApi.h"

#include "../include/QueueEnum.h"
#include "../include/QueueHeader.h"

#include "../include/ApiHeader.h"
#include "../include/ApiStruct.h"

#include "../include/toolkit.h"

#include "../QuantBox_Queue/MsgQueue.h"

#include "TypeConvert.h"

#include <cstring>
#include <assert.h>

void* __stdcall Query(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	// 由内部调用，不用检查是否为空
	CTraderApi* pApi = (CTraderApi*)pApi1;
	pApi->QueryInThread(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
	return nullptr;
}

void CTraderApi::QueryInThread(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	int iRet = 0;
	switch (type)
	{
	case E_Init:
		iRet = _Init();
		break;
	case E_ReqAuthenticateField:
		iRet = _ReqAuthenticate(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_ReqUserLoginField:
		iRet = _ReqUserLogin(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_SettlementInfoConfirmField:
		iRet = _ReqSettlementInfoConfirm(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryTradingAccountField:
		iRet = _ReqQryTradingAccount(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryInvestorPositionField:
		iRet = _ReqQryInvestorPosition(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryInstrumentField:
		iRet = _ReqQryInstrument(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryInvestorField:
		iRet = _ReqQryInvestor(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryOrderField:
		iRet = _ReqQryOrder(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryTradeField:
		iRet = _ReqQryTrade(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_QryQuoteField:
		iRet = _ReqQryQuote(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	default:
		break;
	}

	if (0 == iRet)
	{
		//返回成功，填加到已发送池
		m_nSleep = 1;
	}
	else
	{
		m_msgQueue_Query->Input(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		//失败，按4的幂进行延时，但不超过1s
		m_nSleep *= 4;
		m_nSleep %= 1023;
	}
	this_thread::sleep_for(chrono::milliseconds(m_nSleep));
}

void CTraderApi::Register(void* pCallback)
{
	if (m_msgQueue == nullptr)
		return;

	m_msgQueue_Query->Register((void*)Query);
	m_msgQueue->Register(pCallback);
	if (pCallback)
	{
		m_msgQueue_Query->StartThread();
		m_msgQueue->StartThread();
	}
	else
	{
		m_msgQueue_Query->StopThread();
		m_msgQueue->StopThread();
	}
}

CTraderApi::CTraderApi(void)
{
	m_pApi = nullptr;
	m_lRequestID = 0;
	m_nSleep = 1;

	// 自己维护两个消息队列
	m_msgQueue = new CMsgQueue();
	m_msgQueue_Query = new CMsgQueue();

	m_msgQueue_Query->Register((void*)Query);
	m_msgQueue_Query->StartThread();
}


CTraderApi::~CTraderApi(void)
{
	Disconnect();
}

bool CTraderApi::IsErrorRspInfo(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));
	if (bRet)
	{
		ErrorField field = { 0 };
		field.ErrorID = pRspInfo->ErrorID;
		strcpy(field.ErrorMsg, pRspInfo->ErrorMsg);

		m_msgQueue->Input(ResponeType::OnRtnError, m_msgQueue, this, bIsLast, 0, &field, sizeof(ErrorField), nullptr, 0, nullptr, 0);
	}
	return bRet;
}

bool CTraderApi::IsErrorRspInfo(CThostFtdcRspInfoField *pRspInfo)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));

	return bRet;
}

void CTraderApi::Connect(const string& szPath,
	ServerInfoField* pServerInfo,
	UserInfoField* pUserInfo)
{
	m_szPath = szPath;
	memcpy(&m_ServerInfo, pServerInfo, sizeof(ServerInfoField));
	memcpy(&m_UserInfo, pUserInfo, sizeof(UserInfoField));

	m_msgQueue_Query->Input(RequestType::E_Init, this, nullptr, 0, 0,
		nullptr, 0, nullptr, 0, nullptr, 0);
}

int CTraderApi::_Init()
{
	char *pszPath = new char[m_szPath.length() + 1024];
	srand((unsigned int)time(nullptr));
	sprintf(pszPath, "%s/%s/%s/Td/%d/", m_szPath.c_str(), m_ServerInfo.BrokerID, m_UserInfo.UserID, rand());
	makedirs(pszPath);

	m_pApi = CThostFtdcTraderApi::CreateFtdcTraderApi(pszPath);
	delete[] pszPath;

	m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Initialized, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	if (m_pApi)
	{
		m_pApi->RegisterSpi(this);

		//添加地址
		size_t len = strlen(m_ServerInfo.Address) + 1;
		char* buf = new char[len];
		strncpy(buf, m_ServerInfo.Address, len);

		char* token = strtok(buf, _QUANTBOX_SEPS_);
		while (token)
		{
			if (strlen(token)>0)
			{
				m_pApi->RegisterFront(token);
			}
			token = strtok(nullptr, _QUANTBOX_SEPS_);
		}
		delete[] buf;

		if (m_ServerInfo.PublicTopicResumeType<ResumeType::Undefined)
			m_pApi->SubscribePublicTopic((THOST_TE_RESUME_TYPE)m_ServerInfo.PublicTopicResumeType);
		if (m_ServerInfo.PrivateTopicResumeType<ResumeType::Undefined)
			m_pApi->SubscribePrivateTopic((THOST_TE_RESUME_TYPE)m_ServerInfo.PrivateTopicResumeType);

		//初始化连接
		m_pApi->Init();
		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Connecting, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	}

	return 0;
}

void CTraderApi::OnFrontConnected()
{
	m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Connected, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	//连接成功后自动请求认证或登录
	if (strlen(m_ServerInfo.AuthCode)>0
		&& strlen(m_ServerInfo.UserProductInfo)>0)
	{
		//填了认证码就先认证
		ReqAuthenticate();
	}
	else
	{
		ReqUserLogin();
	}
}

void CTraderApi::OnFrontDisconnected(int nReason)
{
	RspUserLoginField field = { 0 };
	//连接失败返回的信息是拼接而成，主要是为了统一输出
	field.ErrorID = nReason;
	GetOnFrontDisconnectedMsg(nReason, field.ErrorMsg);

	m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
}

void CTraderApi::ReqAuthenticate()
{
	CThostFtdcReqAuthenticateField body = { 0 };

	strncpy(body.BrokerID, m_ServerInfo.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.UserID, m_UserInfo.UserID, sizeof(TThostFtdcInvestorIDType));
	strncpy(body.UserProductInfo, m_ServerInfo.UserProductInfo, sizeof(TThostFtdcProductInfoType));
	strncpy(body.AuthCode, m_ServerInfo.AuthCode, sizeof(TThostFtdcAuthCodeType));

	m_msgQueue_Query->Input(RequestType::E_ReqAuthenticateField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcReqAuthenticateField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqAuthenticate(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Authorizing, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqAuthenticate((CThostFtdcReqAuthenticateField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo)
		&& pRspAuthenticateField)
	{
		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Authorized, 0, nullptr, 0, nullptr, 0, nullptr, 0);

		ReqUserLogin();
	}
	else
	{
		RspUserLoginField field = { 0 };
		field.ErrorID = pRspInfo->ErrorID;
		strncpy(field.ErrorMsg, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));

		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::ReqUserLogin()
{
	CThostFtdcReqUserLoginField body = { 0 };

	strncpy(body.BrokerID, m_ServerInfo.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.UserID, m_UserInfo.UserID, sizeof(TThostFtdcInvestorIDType));
	strncpy(body.Password, m_UserInfo.Password, sizeof(TThostFtdcPasswordType));
	strncpy(body.UserProductInfo, m_ServerInfo.UserProductInfo, sizeof(TThostFtdcProductInfoType));

	m_msgQueue_Query->Input(RequestType::E_ReqUserLoginField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcReqUserLoginField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqUserLogin(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Logining, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqUserLogin((CThostFtdcReqUserLoginField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	RspUserLoginField field = { 0 };

	if (!IsErrorRspInfo(pRspInfo)
		&& pRspUserLogin)
	{
		GetExchangeTime(pRspUserLogin->TradingDay, nullptr, pRspUserLogin->LoginTime,
			&field.TradingDay, nullptr, &field.LoginTime, nullptr);
		sprintf(field.SessionID, "%d:%d", pRspUserLogin->FrontID, pRspUserLogin->SessionID);

		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Logined, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);

		// 记下登录信息，可能会用到
		memcpy(&m_RspUserLogin, pRspUserLogin, sizeof(CThostFtdcRspUserLoginField));
		m_nMaxOrderRef = atol(pRspUserLogin->MaxOrderRef);
		// 自己发单时ID从1开始，不能从0开始
		m_nMaxOrderRef = m_nMaxOrderRef>1 ? m_nMaxOrderRef : 1;
		ReqSettlementInfoConfirm();
		ReqQryInvestor();
	}
	else
	{
		field.ErrorID = pRspInfo->ErrorID;
		strncpy(field.ErrorMsg, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));

		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::ReqSettlementInfoConfirm()
{
	CThostFtdcSettlementInfoConfirmField body = { 0 };

	strncpy(body.BrokerID, m_ServerInfo.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_UserInfo.UserID, sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input(RequestType::E_SettlementInfoConfirmField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcSettlementInfoConfirmField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqSettlementInfoConfirm(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Confirming, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqSettlementInfoConfirm((CThostFtdcSettlementInfoConfirmField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo)
		&& pSettlementInfoConfirm)
	{
		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Confirmed, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Done, 0, nullptr, 0, nullptr, 0, nullptr, 0);

		if (m_ServerInfo.PrivateTopicResumeType > ResumeType::Restart)
		{
			ReqQryOrder();
			//ReqQryTrade();
			ReqQryQuote();
		}
	}
	else
	{
		RspUserLoginField field = { 0 };
		field.ErrorID = pRspInfo->ErrorID;
		strncpy(field.ErrorMsg, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));

		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, &field, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::Disconnect()
{
	if (m_msgQueue_Query)
	{
		m_msgQueue_Query->StopThread();
		m_msgQueue_Query->Register(nullptr);
		m_msgQueue_Query->Clear();
		delete m_msgQueue_Query;
		m_msgQueue_Query = nullptr;
	}

	if(m_pApi)
	{
		m_pApi->RegisterSpi(nullptr);
		m_pApi->Release();
		m_pApi = nullptr;

		// 全清理，只留最后一个
		m_msgQueue->Clear();
		m_msgQueue->Input(ResponeType::OnConnectionStatus, m_msgQueue, this, ConnectionStatus::Disconnected, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		// 主动触发
		m_msgQueue->Process();
	}

	if (m_msgQueue)
	{
		m_msgQueue->StopThread();
		m_msgQueue->Register(nullptr);
		m_msgQueue->Clear();
		delete m_msgQueue;
		m_msgQueue = nullptr;
	}

	m_lRequestID = 0;
}

char* CTraderApi::ReqOrderInsert(
	int OrderRef,
	OrderField* pOrder1,
	OrderField* pOrder2)
{
	if (nullptr == m_pApi)
		return nullptr;

	CThostFtdcInputOrderField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));

	body.MinVolume = 1;
	body.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
	body.IsAutoSuspend = 0;
	body.UserForceClose = 0;
	body.IsSwapOrder = 0;

	//合约
	strncpy(body.InstrumentID, pOrder1->InstrumentID, sizeof(TThostFtdcInstrumentIDType));
	//买卖
	body.Direction = OrderSide_2_TThostFtdcDirectionType(pOrder1->Side);
	//开平
	body.CombOffsetFlag[0] = OpenCloseType_2_TThostFtdcOffsetFlagType(pOrder1->OpenClose);
	//投保
	body.CombHedgeFlag[0] = HedgeFlagType_2_TThostFtdcHedgeFlagType(pOrder1->HedgeFlag);
	//数量
	body.VolumeTotalOriginal = (int)pOrder1->Qty;

	// 对于套利单，是用第一个参数的价格，还是用两个参数的价格差呢？
	body.LimitPrice = pOrder1->Price;
	body.StopPrice = pOrder1->StopPx;

	// 针对第二个进行处理，如果有第二个参数，认为是交易所套利单
	if (pOrder2)
	{
		body.CombOffsetFlag[1] = OpenCloseType_2_TThostFtdcOffsetFlagType(pOrder1->OpenClose);
		body.CombHedgeFlag[1] = HedgeFlagType_2_TThostFtdcHedgeFlagType(pOrder1->HedgeFlag);
		// 交易所的移仓换月功能，没有实测过
		body.IsSwapOrder = (body.CombOffsetFlag[0] != body.CombOffsetFlag[1]);
	}

	// 市价与限价
	switch (pOrder1->Type)
	{
	case Market:
	case Stop:
	case MarketOnClose:
	case TrailingStop:
		body.OrderPriceType = THOST_FTDC_OPT_AnyPrice;
		body.TimeCondition = THOST_FTDC_TC_IOC;
		body.LimitPrice = 0;
		break;
	case Limit:
	case StopLimit:
	case TrailingStopLimit:
	default:
		body.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
		body.TimeCondition = THOST_FTDC_TC_GFD;
		break;
	}

	// IOC与FOK
	switch (pOrder1->TimeInForce)
	{
	case IOC:
		body.TimeCondition = THOST_FTDC_TC_IOC;
		body.VolumeCondition = THOST_FTDC_VC_AV;
		break;
	case FOK:
		body.TimeCondition = THOST_FTDC_TC_IOC;
		body.VolumeCondition = THOST_FTDC_VC_CV;
		//body.MinVolume = body.VolumeTotalOriginal; // 这个地方必须加吗？
		break;
	default:
		body.VolumeCondition = THOST_FTDC_VC_AV;
		break;
	}

	// 条件单
	switch (pOrder1->Type)
	{
	case Stop:
	case TrailingStop:
	case StopLimit:
	case TrailingStopLimit:
		// 条件单没有测试，先留空
		body.ContingentCondition = THOST_FTDC_CC_Immediately;
		break;
	default:
		body.ContingentCondition = THOST_FTDC_CC_Immediately;
		break;
	}

	int nRet = 0;
	{
		//可能报单太快，m_nMaxOrderRef还没有改变就提交了
		lock_guard<mutex> cl(m_csOrderRef);

		if (OrderRef < 0)
		{
			nRet = m_nMaxOrderRef;
			++m_nMaxOrderRef;
		}
		else
		{
			nRet = OrderRef;
		}
		sprintf(body.OrderRef, "%d", nRet);

		// 测试平台穿越速度，用完后需要注释掉
		//WriteLog("CTP:ReqOrderInsert:%s %d", body.InstrumentID, nRet);


		//不保存到队列，而是直接发送
		int n = m_pApi->ReqOrderInsert(&body, ++m_lRequestID);
		if (n < 0)
		{
			nRet = n;
			return nullptr;
		}
		else
		{
			// 用于各种情况下找到原订单，用于进行响应的通知
			sprintf(m_orderInsert_Id, "%d:%d:%d", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, nRet);

			OrderField* pField = new OrderField();
			memcpy(pField, pOrder1, sizeof(OrderField));
			strcpy(pField->ID, m_orderInsert_Id);
			m_id_platform_order.insert(pair<string, OrderField*>(m_orderInsert_Id, pField));
		}
	}

	return m_orderInsert_Id;
}

void CTraderApi::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType orderId = { 0 };

	if (pInputOrder)
	{
		sprintf(orderId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputOrder->OrderRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		pField->ExecType = ExecType::ExecRejected;
		pField->Status = OrderStatus::Rejected;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnOrder, m_msgQueue, this, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType orderId = { 0 };

	if (pInputOrder)
	{
		sprintf(orderId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputOrder->OrderRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, 0, true);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		pField->ExecType = ExecType::ExecRejected;
		pField->Status = OrderStatus::Rejected;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnOrder, m_msgQueue, this, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRtnTrade(CThostFtdcTradeField *pTrade)
{
	OnTrade(pTrade);
}

int CTraderApi::ReqOrderAction(const string& szId)
{
	unordered_map<string, CThostFtdcOrderField*>::iterator it = m_id_api_order.find(szId);
	if (it == m_id_api_order.end())
	{
		// <error id="ORDER_NOT_FOUND" value="25" prompt="CTP:撤单找不到相应报单"/>
		//ErrorField field = { 0 };
		//field.ErrorID = 25;
		//sprintf(field.ErrorMsg, "ORDER_NOT_FOUND");

		////TODO:应当通过报单回报通知订单找不到

		//XRespone(ResponeType::OnRtnError, m_msgQueue, this, 0, 0, &field, sizeof(ErrorField), nullptr, 0, nullptr, 0);
		return -100;
	}
	else
	{
		// 找到了订单
		return ReqOrderAction(it->second);
	}
}

int CTraderApi::ReqOrderAction(CThostFtdcOrderField *pOrder)
{
	if (nullptr == m_pApi)
		return 0;

	CThostFtdcInputOrderActionField body = {0};

	///经纪公司代码
	strncpy(body.BrokerID, pOrder->BrokerID,sizeof(TThostFtdcBrokerIDType));
	///投资者代码
	strncpy(body.InvestorID, pOrder->InvestorID,sizeof(TThostFtdcInvestorIDType));
	///报单引用
	strncpy(body.OrderRef, pOrder->OrderRef,sizeof(TThostFtdcOrderRefType));
	///前置编号
	body.FrontID = pOrder->FrontID;
	///会话编号
	body.SessionID = pOrder->SessionID;
	///交易所代码
	strncpy(body.ExchangeID,pOrder->ExchangeID,sizeof(TThostFtdcExchangeIDType));
	///报单编号
	strncpy(body.OrderSysID,pOrder->OrderSysID,sizeof(TThostFtdcOrderSysIDType));
	///操作标志
	body.ActionFlag = THOST_FTDC_AF_Delete;
	///合约代码
	strncpy(body.InstrumentID, pOrder->InstrumentID,sizeof(TThostFtdcInstrumentIDType));

	int nRet = m_pApi->ReqOrderAction(&body, ++m_lRequestID);
	return nRet;
}

void CTraderApi::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType orderId = { 0 };
	if (pInputOrderAction)
	{
		sprintf(orderId, "%d:%d:%s", pInputOrderAction->FrontID, pInputOrderAction->SessionID, pInputOrderAction->OrderRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		strcpy(pField->ID, orderId);
		pField->ExecType = ExecType::ExecCancelReject;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnOrder, m_msgQueue, this, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnOrderAction(CThostFtdcOrderActionField *pOrderAction, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType orderId = { 0 };
	if (pOrderAction)
	{
		sprintf(orderId, "%d:%d:%s", pOrderAction->FrontID, pOrderAction->SessionID, pOrderAction->OrderRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, 0, true);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		strcpy(pField->ID, orderId);
		pField->ExecType = ExecType::ExecCancelReject;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnOrder, m_msgQueue, this, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRtnOrder(CThostFtdcOrderField *pOrder)
{
	OnOrder(pOrder);
}

char* CTraderApi::ReqQuoteInsert(
	int QuoteRef,
	QuoteField* pQuote)
{
	if (nullptr == m_pApi)
		return nullptr;

	CThostFtdcInputQuoteField body = {0};

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	//合约,目前只从订单1中取
	strncpy(body.InstrumentID, pQuote->InstrumentID, sizeof(TThostFtdcInstrumentIDType));
	//开平
	body.AskOffsetFlag = OpenCloseType_2_TThostFtdcOffsetFlagType(pQuote->AskOpenClose);
	body.BidOffsetFlag = OpenCloseType_2_TThostFtdcOffsetFlagType(pQuote->BidOpenClose);
	//投保
	body.AskHedgeFlag = HedgeFlagType_2_TThostFtdcHedgeFlagType(pQuote->AskHedgeFlag);
	body.BidHedgeFlag = HedgeFlagType_2_TThostFtdcHedgeFlagType(pQuote->BidHedgeFlag);

	//价格
	body.AskPrice = pQuote->AskPrice;
	body.BidPrice = pQuote->BidPrice;

	//数量
	body.AskVolume = (int)pQuote->AskQty;
	body.BidVolume = (int)pQuote->BidQty;

	strncpy(body.ForQuoteSysID, pQuote->QuoteReqID, sizeof(TThostFtdcOrderSysIDType));

	int nRet = 0;
	{
		//可能报单太快，m_nMaxOrderRef还没有改变就提交了
		lock_guard<mutex> cl(m_csOrderRef);

		if (QuoteRef < 0)
		{
			nRet = m_nMaxOrderRef;
			sprintf(body.QuoteRef, "%d", m_nMaxOrderRef);
			sprintf(body.AskOrderRef, "%d", m_nMaxOrderRef);
			sprintf(body.BidOrderRef, "%d", ++m_nMaxOrderRef);
			++m_nMaxOrderRef;
		}
		else
		{
			nRet = QuoteRef;
			sprintf(body.QuoteRef, "%d", QuoteRef);
			sprintf(body.AskOrderRef, "%d", QuoteRef);
			sprintf(body.BidOrderRef, "%d", ++QuoteRef);
			++QuoteRef;
		}

		//不保存到队列，而是直接发送
		int n = m_pApi->ReqQuoteInsert(&body, ++m_lRequestID);
		if (n < 0)
		{
			nRet = n;
			return nullptr;
		}
		else
		{
			sprintf(m_orderInsert_Id, "%d:%d:%d", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, nRet);

			QuoteField* pField = new QuoteField();
			memcpy(pField, pQuote, sizeof(QuoteField));
			strcpy(pField->ID, m_orderInsert_Id);
			strcpy(pField->AskID, m_orderInsert_Id);
			sprintf(pField->BidID, "%d:%d:%d", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, nRet + 1);

			m_id_platform_quote.insert(pair<string, QuoteField*>(m_orderInsert_Id, pField));
		}
	}

	return m_orderInsert_Id;
}

void CTraderApi::OnRspQuoteInsert(CThostFtdcInputQuoteField *pInputQuote, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType quoteId = { 0 };

	if (pInputQuote)
	{
		sprintf(quoteId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputQuote->QuoteRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		pField->ExecType = ExecType::ExecRejected;
		pField->Status = OrderStatus::Rejected;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnQuote, m_msgQueue, this, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnQuoteInsert(CThostFtdcInputQuoteField *pInputQuote, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType quoteId = { 0 };

	if (pInputQuote)
	{
		sprintf(quoteId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputQuote->QuoteRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, 0, true);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		pField->ExecType = ExecType::ExecRejected;
		pField->Status = OrderStatus::Rejected;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnQuote, m_msgQueue, this, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRtnQuote(CThostFtdcQuoteField *pQuote)
{
	OnQuote(pQuote);
}

int CTraderApi::ReqQuoteAction(const string& szId)
{
	unordered_map<string, CThostFtdcQuoteField*>::iterator it = m_id_api_quote.find(szId);
	if (it == m_id_api_quote.end())
	{
		//// <error id="QUOTE_NOT_FOUND" value="86" prompt="CTP:报价撤单找不到相应报价"/>
		return -100;
	}
	else
	{
		// 找到了订单
		ReqQuoteAction(it->second);
	}
	return 0;
}

int CTraderApi::ReqQuoteAction(CThostFtdcQuoteField *pQuote)
{
	if (nullptr == m_pApi)
		return 0;

	CThostFtdcInputQuoteActionField body = {0};

	///经纪公司代码
	strcpy(body.BrokerID, pQuote->BrokerID);
	///投资者代码
	strcpy(body.InvestorID, pQuote->InvestorID);
	///报单引用
	strcpy(body.QuoteRef, pQuote->QuoteRef);
	///前置编号
	body.FrontID = pQuote->FrontID;
	///会话编号
	body.SessionID = pQuote->SessionID;
	///交易所代码
	strcpy(body.ExchangeID, pQuote->ExchangeID);
	///报单编号
	strcpy(body.QuoteSysID, pQuote->QuoteSysID);
	///操作标志
	body.ActionFlag = THOST_FTDC_AF_Delete;
	///合约代码
	strcpy(body.InstrumentID, pQuote->InstrumentID);

	int nRet = m_pApi->ReqQuoteAction(&body, ++m_lRequestID);
	return nRet;
}

void CTraderApi::OnRspQuoteAction(CThostFtdcInputQuoteActionField *pInputQuoteAction, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType quoteId = { 0 };
	if (pInputQuoteAction)
	{
		sprintf(quoteId, "%d:%d:%s", pInputQuoteAction->FrontID, pInputQuoteAction->SessionID, pInputQuoteAction->QuoteRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		strcpy(pField->ID, quoteId);
		//sprintf(pField->AskID, "%d:%d:%s", pInputQuoteAction->FrontID, pInputQuoteAction->SessionID, pInputQuoteAction->);
		//sprintf(pField->BidID, "%d:%d:%s", pInputQuoteAction->FrontID, pInputQuoteAction->SessionID, pInputQuoteAction->QuoteRef);
		pField->ExecType = ExecType::ExecCancelReject;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnQuote, m_msgQueue, this, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnQuoteAction(CThostFtdcQuoteActionField *pQuoteAction, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType quoteId = { 0 };

	if (pQuoteAction)
	{
		sprintf(quoteId, "%d:%d:%s", pQuoteAction->FrontID, pQuoteAction->SessionID, pQuoteAction->QuoteRef);
	}
	else
	{
		IsErrorRspInfo(pRspInfo, 0, true);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		strcpy(pField->ID, quoteId);
		pField->ExecType = ExecType::ExecCancelReject;
		pField->ErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(ErrorMsgType));
		m_msgQueue->Input(ResponeType::OnRtnQuote, m_msgQueue, this, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::ReqQryTradingAccount()
{
	CThostFtdcQryTradingAccountField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input(RequestType::E_QryTradingAccountField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryTradingAccountField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryTradingAccount(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryTradingAccount((CThostFtdcQryTradingAccountField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		if (pTradingAccount)
		{
			AccountField field = { 0 };
			field.PreBalance = pTradingAccount->PreBalance;
			field.CurrMargin = pTradingAccount->CurrMargin;
			field.Commission = pTradingAccount->Commission;
			field.CloseProfit = pTradingAccount->CloseProfit;
			field.PositionProfit = pTradingAccount->PositionProfit;
			field.Balance = pTradingAccount->Balance;
			field.Available = pTradingAccount->Available;

			m_msgQueue->Input(ResponeType::OnRspQryTradingAccount, m_msgQueue, this, bIsLast, 0, &field, sizeof(AccountField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input(ResponeType::OnRspQryTradingAccount, m_msgQueue, this, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

void CTraderApi::ReqQryInvestorPosition(const string& szInstrumentId, const string& szExchange)
{
	CThostFtdcQryInvestorPositionField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
	strncpy(body.InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));

	m_msgQueue_Query->Input(RequestType::E_QryInvestorPositionField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryInvestorPositionField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryInvestorPosition(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryInvestorPosition((CThostFtdcQryInvestorPositionField*)ptr1, ++m_lRequestID);
}

// 如果是请求查询，就将数据全部返回
// 如果是后期的成交回报，就只返回更新的记录
// 对于中金所，同时有今昨两天的持仓时，只返回今天的两条多空数据
// 对于上期所，目前没条件测，当成是也只有两条
void CTraderApi::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		if (pInvestorPosition)
		{
			PositionIDType positionId = { 0 };
			sprintf(positionId, "%s:%d:%c",
				pInvestorPosition->InstrumentID, TThostFtdcPosiDirectionType_2_PositionSide(pInvestorPosition->PosiDirection), pInvestorPosition->HedgeFlag);

			PositionField* pField = nullptr;
			unordered_map<string, PositionField*>::iterator it = m_id_platform_position.find(positionId);
			if (it == m_id_platform_position.end())
			{
				pField = new PositionField();
				memset(pField, 0, sizeof(PositionField));

				strcpy(pField->Symbol, pInvestorPosition->InstrumentID);
				strcpy(pField->InstrumentID, pInvestorPosition->InstrumentID);
				//strcpy(pField->ExchangeID, );
				pField->Side = TThostFtdcPosiDirectionType_2_PositionSide(pInvestorPosition->PosiDirection);
				pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pInvestorPosition->HedgeFlag);

				m_id_platform_position.insert(pair<string, PositionField*>(positionId, pField));
			}
			else
			{
				pField = it->second;
			}

			pField->Position = pInvestorPosition->Position;
			pField->TdPosition = pInvestorPosition->TodayPosition;
			pField->YdPosition = pInvestorPosition->Position - pInvestorPosition->TodayPosition;

			// 等数据收集全了再遍历通知一次，为何要这样做？因为今昨是两条记录，但我记在一个里面
			if (bIsLast)
			{
				int cnt = 0;
				size_t count = m_id_platform_position.size();
				for (unordered_map<string, PositionField*>::iterator iter = m_id_platform_position.begin(); iter != m_id_platform_position.end(); iter++)
				{
					++cnt;
					m_msgQueue->Input(ResponeType::OnRspQryInvestorPosition, m_msgQueue, this, cnt == count, 0, iter->second, sizeof(PositionField), nullptr, 0, nullptr, 0);
				}
			}
			//XRespone(ResponeType::OnRspQryInvestorPosition, m_msgQueue, this, bIsLast, 0, pField, sizeof(PositionField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input(ResponeType::OnRspQryInvestorPosition, m_msgQueue, this, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

void CTraderApi::ReqQryInstrument(const string& szInstrumentId, const string& szExchange)
{
	CThostFtdcQryInstrumentField body = {0};

	strncpy(body.InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
	strncpy(body.ExchangeID, szExchange.c_str(), sizeof(TThostFtdcExchangeIDType));

	m_msgQueue_Query->Input(RequestType::E_QryInstrumentField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryInstrumentField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryInstrument(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryInstrument((CThostFtdcQryInstrumentField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		if (pInstrument)
		{
			InstrumentField field = { 0 };

			strncpy(field.InstrumentID, pInstrument->InstrumentID, sizeof(InstrumentIDType));
			strncpy(field.ExchangeID, pInstrument->ExchangeID, sizeof(ExchangeIDType));

			strncpy(field.Symbol, pInstrument->InstrumentID, sizeof(SymbolType));

			strncpy(field.InstrumentName, pInstrument->InstrumentName, sizeof(InstrumentNameType));
			field.Type = CThostFtdcInstrumentField_2_InstrumentType(pInstrument);
			field.VolumeMultiple = pInstrument->VolumeMultiple;
			field.PriceTick = pInstrument->PriceTick;
			strncpy(field.ExpireDate, pInstrument->ExpireDate, sizeof(DateType));
			field.OptionsType = TThostFtdcOptionsTypeType_2_PutCall(pInstrument->OptionsType);
			field.StrikePrice = pInstrument->StrikePrice;

			m_msgQueue->Input(ResponeType::OnRspQryInstrument, m_msgQueue, this, bIsLast, 0, &field, sizeof(InstrumentField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input(ResponeType::OnRspQryInstrument, m_msgQueue, this, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

//void CTraderApi::ReqQryInstrumentCommissionRate(const string& szInstrumentId)
//{
//	CThostFtdcQryInstrumentCommissionRateField body = {0};
//
//	strncpy(body.BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
//	strncpy(body.InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
//	strncpy(body.InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
//}
//
//void CTraderApi::OnRspQryInstrumentCommissionRate(CThostFtdcInstrumentCommissionRateField *pInstrumentCommissionRate, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//
//}

//void CTraderApi::ReqQryInstrumentMarginRate(const string& szInstrumentId,TThostFtdcHedgeFlagType HedgeFlag)
//{
//	CThostFtdcQryInstrumentMarginRateField body = {0};
//
//	strncpy(body.BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
//	strncpy(body.InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
//	strncpy(body.InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
//	body.HedgeFlag = HedgeFlag;
//
//	//AddToSendQueue(pRequest);
//}

//void CTraderApi::OnRspQryInstrumentMarginRate(CThostFtdcInstrumentMarginRateField *pInstrumentMarginRate, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//
//}
//
//void CTraderApi::ReqQrySettlementInfo(const string& szTradingDay)
//{
//	if (nullptr == m_pApi)
//		return;
//
//	CThostFtdcQrySettlementInfoField body = {0};
//
//	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
//	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
//	strncpy(body.TradingDay, szTradingDay.c_str(), sizeof(TThostFtdcDateType));
//}
//
//void CTraderApi::OnRspQrySettlementInfo(CThostFtdcSettlementInfoField *pSettlementInfo, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
//	{
//		if (pSettlementInfo)
//		{
//			SettlementInfoField field = { 0 };
//			strncpy(field.TradingDay, pSettlementInfo->TradingDay, sizeof(TThostFtdcDateType));
//			strncpy(field.Content, pSettlementInfo->Content, sizeof(TThostFtdcContentType));
//
//			m_msgQueue->Input(ResponeType::OnRspQrySettlementInfo, m_msgQueue, this, bIsLast, 0, &field, sizeof(SettlementInfoField), nullptr, 0, nullptr, 0);
//		}
//		else
//		{
//			m_msgQueue->Input(ResponeType::OnRspQrySettlementInfo, m_msgQueue, this, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
//		}
//	}
//}

void CTraderApi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	IsErrorRspInfo(pRspInfo, nRequestID, bIsLast);
}

void CTraderApi::ReqQryOrder()
{
	CThostFtdcQryOrderField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input(RequestType::E_QryOrderField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryOrderField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryOrder(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryOrder((CThostFtdcQryOrderField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnOrder(CThostFtdcOrderField *pOrder)
{
	if (nullptr == pOrder)
		return;

	OrderIDType orderId = { 0 };
	sprintf(orderId, "%d:%d:%s", pOrder->FrontID, pOrder->SessionID, pOrder->OrderRef);
	OrderIDType orderSydId = { 0 };

	{
		// 保存原始订单信息，用于撤单

		unordered_map<string, CThostFtdcOrderField*>::iterator it = m_id_api_order.find(orderId);
		if (it == m_id_api_order.end())
		{
			// 找不到此订单，表示是新单
			CThostFtdcOrderField* pField = new CThostFtdcOrderField();
			memcpy(pField, pOrder, sizeof(CThostFtdcOrderField));
			m_id_api_order.insert(pair<string, CThostFtdcOrderField*>(orderId, pField));
		}
		else
		{
			// 找到了订单
			// 需要再复制保存最后一次的状态，还是只要第一次的用于撤单即可？记下，这样最后好比较
			CThostFtdcOrderField* pField = it->second;
			memcpy(pField, pOrder, sizeof(CThostFtdcOrderField));
		}

		// 保存SysID用于定义成交回报与订单
		sprintf(orderSydId, "%s:%s", pOrder->ExchangeID, pOrder->OrderSysID);
		m_sysId_orderId.insert(pair<string, string>(orderSydId, orderId));
	}

	{
		// 从API的订单转换成自己的结构体

		OrderField* pField = nullptr;
		unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
		if (it == m_id_platform_order.end())
		{
			// 开盘时发单信息还没有，所以找不到对应的单子，需要进行Order的恢复
			pField = new OrderField();
			memset(pField, 0, sizeof(OrderField));
			strcpy(pField->ID, orderId);
			strcpy(pField->InstrumentID, pOrder->InstrumentID);
			strcpy(pField->ExchangeID, pOrder->ExchangeID);
			pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pOrder->CombHedgeFlag[0]);
			pField->Side = TThostFtdcDirectionType_2_OrderSide(pOrder->Direction);
			pField->Price = pOrder->LimitPrice;
			pField->StopPx = pOrder->StopPrice;
			strncpy(pField->Text, pOrder->StatusMsg, sizeof(ErrorMsgType));
			pField->OpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pOrder->CombOffsetFlag[0]);
			pField->Status = CThostFtdcOrderField_2_OrderStatus(pOrder);
			pField->Qty = pOrder->VolumeTotalOriginal;
			pField->Type = CThostFtdcOrderField_2_OrderType(pOrder);
			pField->TimeInForce = CThostFtdcOrderField_2_TimeInForce(pOrder);
			pField->ExecType = ExecType::ExecNew;
			strcpy(pField->OrderID, pOrder->OrderSysID);


			// 添加到map中，用于其它工具的读取，撤单失败时的再通知等
			m_id_platform_order.insert(pair<string, OrderField*>(orderId, pField));
		}
		else
		{
			pField = it->second;
			strcpy(pField->ID, orderId);
			pField->LeavesQty = pOrder->VolumeTotal;
			pField->Price = pOrder->LimitPrice;
			pField->Status = CThostFtdcOrderField_2_OrderStatus(pOrder);
			pField->ExecType = CThostFtdcOrderField_2_ExecType(pOrder);
			strcpy(pField->OrderID, pOrder->OrderSysID);
			strncpy(pField->Text, pOrder->StatusMsg, sizeof(ErrorMsgType));
		}

		m_msgQueue->Input(ResponeType::OnRtnOrder, m_msgQueue, this, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRspQryOrder(CThostFtdcOrderField *pOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		OnOrder(pOrder);
	}
}

void CTraderApi::ReqQryTrade()
{
	CThostFtdcQryTradeField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input(RequestType::E_QryTradeField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryTradeField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryTrade(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryTrade((CThostFtdcQryTradeField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnTrade(CThostFtdcTradeField *pTrade)
{
	if (nullptr == pTrade)
		return;

	TradeField* pField = new TradeField();
	strcpy(pField->InstrumentID, pTrade->InstrumentID);
	strcpy(pField->ExchangeID, pTrade->ExchangeID);
	pField->Side = TThostFtdcDirectionType_2_OrderSide(pTrade->Direction);
	pField->Qty = pTrade->Volume;
	pField->Price = pTrade->Price;
	pField->OpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pTrade->OffsetFlag);
	pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pTrade->HedgeFlag);
	pField->Commission = 0;//TODO收续费以后要计算出来
	strcpy(pField->Time, pTrade->TradeTime);
	strcpy(pField->TradeID, pTrade->TradeID);

	OrderIDType orderSysId = { 0 };
	sprintf(orderSysId, "%s:%s", pTrade->ExchangeID, pTrade->OrderSysID);
	unordered_map<string, string>::iterator it = m_sysId_orderId.find(orderSysId);
	if (it == m_sysId_orderId.end())
	{
		// 此成交找不到对应的报单
		//assert(false);
	}
	else
	{
		// 找到对应的报单
		strcpy(pField->ID, it->second.c_str());

		m_msgQueue->Input(ResponeType::OnRtnTrade, m_msgQueue, this, 0, 0, pField, sizeof(TradeField), nullptr, 0, nullptr, 0);

		unordered_map<string, OrderField*>::iterator it2 = m_id_platform_order.find(it->second);
		if (it2 == m_id_platform_order.end())
		{
			// 此成交找不到对应的报单
			//assert(false);
		}
		else
		{
			// 更新订单的状态
			// 是否要通知接口
		}

		OnTrade(pField);
	}
}

void CTraderApi::OnTrade(TradeField *pTrade)
{
	PositionIDType positionId = { 0 };
	sprintf(positionId, "%s:%d:%c",
		pTrade->InstrumentID, TradeField_2_PositionSide(pTrade), pTrade->HedgeFlag);

	PositionField* pField = nullptr;
	unordered_map<string, PositionField*>::iterator it = m_id_platform_position.find(positionId);
	if (it == m_id_platform_position.end())
	{
		pField = new PositionField();
		memset(pField, 0, sizeof(PositionField));

		strcpy(pField->Symbol, pTrade->InstrumentID);
		strcpy(pField->InstrumentID, pTrade->InstrumentID);
		pField->Side = TradeField_2_PositionSide(pTrade);
		pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pTrade->HedgeFlag);

		m_id_platform_position.insert(pair<string, PositionField*>(positionId, pField));
	}
	else
	{
		pField = it->second;
	}

	if (pTrade->OpenClose == OpenCloseType::Open)
	{
		pField->Position += pTrade->Qty;
		pField->TdPosition += pTrade->Qty;
	}
	else
	{
		pField->Position -= pTrade->Qty;
		if (pTrade->OpenClose == OpenCloseType::CloseToday)
		{
			pField->TdPosition -= pTrade->Qty;
		}
		else
		{
			pField->YdPosition -= pTrade->Qty;
			// 如果昨天的被减成负数，从今天开始继续减
			if (pField->YdPosition<0)
			{
				pField->TdPosition += pField->YdPosition;
				pField->YdPosition = 0;
			}
		}

		// 计算错误，直接重新查询
		if (pField->Position < 0 || pField->TdPosition < 0 || pField->YdPosition < 0)
		{
			ReqQryInvestorPosition("", "");
			return;
		}
	}

	m_msgQueue->Input(ResponeType::OnRspQryInvestorPosition, m_msgQueue, this, false, 0, pField, sizeof(PositionField), nullptr, 0, nullptr, 0);
}

void CTraderApi::OnRspQryTrade(CThostFtdcTradeField *pTrade, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		OnTrade(pTrade);
	}
}

void CTraderApi::ReqQryQuote()
{
	CThostFtdcQryQuoteField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input(RequestType::E_QryQuoteField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryQuoteField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryQuote(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryQuote((CThostFtdcQryQuoteField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnQuote(CThostFtdcQuoteField *pQuote)
{
	if (nullptr == pQuote)
		return;

	OrderIDType quoteId = { 0 };
	sprintf(quoteId, "%d:%d:%s", pQuote->FrontID, pQuote->SessionID, pQuote->QuoteRef);
	OrderIDType orderSydId = { 0 };

	{
		// 保存原始订单信息，用于撤单

		unordered_map<string, CThostFtdcQuoteField*>::iterator it = m_id_api_quote.find(quoteId);
		if (it == m_id_api_quote.end())
		{
			// 找不到此订单，表示是新单
			CThostFtdcQuoteField* pField = new CThostFtdcQuoteField();
			memcpy(pField, pQuote, sizeof(CThostFtdcQuoteField));
			m_id_api_quote.insert(pair<string, CThostFtdcQuoteField*>(quoteId, pField));
		}
		else
		{
			// 找到了订单
			// 需要再复制保存最后一次的状态，还是只要第一次的用于撤单即可？记下，这样最后好比较
			CThostFtdcQuoteField* pField = it->second;
			memcpy(pField, pQuote, sizeof(CThostFtdcQuoteField));
		}

		// 这个地方是否要进行其它处理？

		// 保存SysID用于定义成交回报与订单
		//sprintf(orderSydId, "%s:%s", pQuote->ExchangeID, pQuote->QuoteSysID);
		//m_sysId_quoteId.insert(pair<string, string>(orderSydId, quoteId));
	}

	{
		// 从API的订单转换成自己的结构体

		QuoteField* pField = nullptr;
		unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
		if (it == m_id_platform_quote.end())
		{
			// 开盘时发单信息还没有，所以找不到对应的单子，需要进行Order的恢复
			pField = new QuoteField();
			memset(pField, 0, sizeof(QuoteField));
			strcpy(pField->InstrumentID, pQuote->InstrumentID);
			strcpy(pField->ExchangeID, pQuote->ExchangeID);

			pField->AskQty = pQuote->AskVolume;
			pField->AskPrice = pQuote->AskPrice;
			pField->AskOpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pQuote->AskOffsetFlag);
			pField->AskHedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pQuote->AskHedgeFlag);

			pField->BidQty = pQuote->BidVolume;
			pField->BidPrice = pQuote->BidPrice;
			pField->BidOpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pQuote->BidOffsetFlag);
			pField->BidHedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pQuote->BidHedgeFlag);

			strcpy(pField->ID, quoteId);
			strcpy(pField->AskOrderID, pQuote->AskOrderSysID);
			strcpy(pField->BidOrderID, pQuote->BidOrderSysID);

			strncpy(pField->Text, pQuote->StatusMsg, sizeof(ErrorMsgType));

			//pField->ExecType = ExecType::ExecNew;
			pField->Status = CThostFtdcQuoteField_2_OrderStatus(pQuote);
			pField->ExecType = ExecType::ExecNew;


			// 添加到map中，用于其它工具的读取，撤单失败时的再通知等
			m_id_platform_quote.insert(pair<string, QuoteField*>(quoteId, pField));
		}
		else
		{
			pField = it->second;

			strcpy(pField->ID, quoteId);
			strcpy(pField->AskOrderID, pQuote->AskOrderSysID);
			strcpy(pField->BidOrderID, pQuote->BidOrderSysID);

			pField->Status = CThostFtdcQuoteField_2_OrderStatus(pQuote);
			pField->ExecType = CThostFtdcQuoteField_2_ExecType(pQuote);

			strncpy(pField->Text, pQuote->StatusMsg, sizeof(ErrorMsgType));
		}

		m_msgQueue->Input(ResponeType::OnRtnQuote, m_msgQueue, this, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRspQryQuote(CThostFtdcQuoteField *pQuote, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		OnQuote(pQuote);
	}
}

void CTraderApi::OnRtnInstrumentStatus(CThostFtdcInstrumentStatusField *pInstrumentStatus)
{
}

void CTraderApi::ReqQryInvestor()
{
	CThostFtdcQryInvestorField body = { 0 };

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input(RequestType::E_QryInvestorField, this, nullptr, 0, 0,
		&body, sizeof(CThostFtdcQryInvestorField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqQryInvestor(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	return m_pApi->ReqQryInvestor((CThostFtdcQryInvestorField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspQryInvestor(CThostFtdcInvestorField *pInvestor, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo, nRequestID, bIsLast))
	{
		if (pInvestor)
		{
			memcpy(&m_Investor, pInvestor, sizeof(CThostFtdcInvestorField));

			InvestorField field = { 0 };
			strcpy(field.BrokerID, pInvestor->BrokerID);
			strcpy(field.InvestorID, pInvestor->InvestorID);
			strcpy(field.InvestorName, pInvestor->InvestorName);
			strcpy(field.IdentifiedCardNo, pInvestor->IdentifiedCardNo);
			field.IdentifiedCardType = TThostFtdcIdCardTypeType_2_IdCardType(pInvestor->IdentifiedCardType);

			m_msgQueue->Input(ResponeType::OnRspQryInvestor, m_msgQueue, this, bIsLast, 0, &field, sizeof(InvestorField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input(ResponeType::OnRspQryInvestor, m_msgQueue, this, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}