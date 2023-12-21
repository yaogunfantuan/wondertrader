﻿/*!
 * \file EventCaster.cpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#include "MQServer.h"
#include "MQManager.h"

#include "../Share/StrUtil.hpp"
#include "../Share/TimeUtils.hpp"
#include "../Share/fmtlib.h"

#include <atomic>


#ifndef NN_STATIC_LIB
#define NN_STATIC_LIB
#endif
#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>


USING_NS_WTP;


inline uint32_t makeMQSvrId()
{
	static std::atomic<uint32_t> _auto_server_id{ 1001 };
	return _auto_server_id.fetch_add(1);
}


MQServer::MQServer(MQManager* mgr)
	: _sock(-1)
	, _ready(false)
	, _mgr(mgr)
	, _confirm(false)
	, m_bTerminated(false)
	, m_bTimeout(false)
{
	_id = makeMQSvrId();
}

MQServer::~MQServer()
{
	if (!_ready)
		return;

	m_bTerminated = true;
	if (m_thrdCast)
		m_thrdCast->join();

	if (_sock >= 0)
		nn_close(_sock);
}

bool MQServer::init(const char* url, bool confirm /* = false */)
{
	if (_sock >= 0)
		return true;

	_confirm = confirm;

	_sock = nn_socket(AF_SP, NN_PUB);
	if(_sock < 0)
	{
		_mgr->log_server(_id, fmtutil::format("MQServer {} initializing failed: {}", _id, nn_strerror(nn_errno())));
		_sock = -1;
		return false;
	}

	int bufsize = 8 * 1024 * 1024;
	if(nn_setsockopt(_sock, NN_SOL_SOCKET, NN_SNDBUF, &bufsize, sizeof(bufsize)) < 0)
	{
		_mgr->log_server(_id, fmtutil::format("MQServer {} setsockopt failed: {}", _id, nn_strerror(nn_errno())));
		nn_close(_sock);
		_sock = -1;
		return false;
	}

	_url = url;
	int ec = nn_bind(_sock, url);
	if(ec < 0)
	{
		_mgr->log_server(_id, fmtutil::format("MQServer {} binding url {} failed: {}", _id, url, nn_strerror(nn_errno())));
		nn_close(_sock);
		_sock = -1;
		return false;
	}
	else
	{
		_mgr->log_server(_id, fmtutil::format("MQServer {} has binded to {} ", _id, url));
	}

	_ready = true;

	return true;
}

void MQServer::publish(const char* topic, const void* data, uint32_t dataLen)
{
	if(_sock < 0)
	{
		_mgr->log_server(_id, fmtutil::format("MQServer {} has not been initialized yet", _id));
		return;
	}

	if(data == NULL || dataLen == 0 || m_bTerminated)
		return;

	{
		SpinLock lock(m_mtxCast);
		m_dataQue.emplace_back(PubData(topic, data, dataLen));
		m_uLastHBTime = TimeUtils::getLocalTimeNow();
	}

	if(m_thrdCast == NULL)
	{
		m_thrdCast.reset(new StdThread([this](){

			if (m_sendBuf.empty())
				m_sendBuf.resize(1024 * 1024, 0);
			m_uLastHBTime = TimeUtils::getLocalTimeNow();
			while (!m_bTerminated)
			{
				int cnt = (int)nn_get_statistic(_sock, NN_STAT_CURRENT_CONNECTIONS);
				if(m_dataQue.empty() || (cnt == 0 && _confirm))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(2));

					m_bTimeout = true;
					uint64_t now = TimeUtils::getLocalTimeNow();
					//如果有连接，并且超过60s没有新的数据推送，就推送一条心跳包
					if (now - m_uLastHBTime > 60*1000 && cnt>0)
					{
						//等待超时以后，广播心跳包
						m_dataQue.emplace_back(PubData("HEARTBEAT", "", 0));
						m_uLastHBTime = now;
					}
					else
					{
						continue;
					}
				}	

				PubDataQue tmpQue;
				{
					SpinLock lock(m_mtxCast);
					tmpQue.swap(m_dataQue);
				}
				
				for(const PubData& pubData : tmpQue)
				{
					if (!pubData._data.empty())
					{
						std::size_t len = sizeof(MQPacket) + pubData._data.size();
						if (m_sendBuf.size() < len)
							m_sendBuf.resize(m_sendBuf.size() * 2);
						MQPacket* pack = (MQPacket*)m_sendBuf.data();
						strncpy(pack->_topic, pubData._topic.c_str(), 32);
						pack->_length = (uint32_t)pubData._data.size();
						memcpy(&pack->_data, pubData._data.data(), pubData._data.size());
						int bytes_snd = 0;
						for(;;)
						{
							int bytes = nn_send(_sock, m_sendBuf.data() + bytes_snd, len - bytes_snd, 0);
							if (bytes >= 0)
							{
								bytes_snd += bytes;
							}
                            else
                            {
                                _mgr->log_server(_id, fmtutil::format("Publishing error: {}", nn_strerror(nn_errno())));
                            }
                            
                            if(bytes_snd == len)
								break;
							else
								std::this_thread::sleep_for(std::chrono::milliseconds(1));
						}
						
					}
				} 

				_mgr->log_server(_id, fmtutil::format("Publishing finished: {}", tmpQue.size()));
			}
		}));
	}
}