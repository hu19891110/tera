// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TERA_RPC_CLIENT_H
#define TERA_RPC_CLIENT_H

#include <string>

#include <glog/logging.h>
#include <sofa/pbrpc/pbrpc.h>
#include "common/base/scoped_ptr.h"
#include "common/net/ip_address.h"
#include "common/this_thread.h"

#include "tera/proto/status_code.pb.h"
#include "tera/proto/proto_helper.h"

namespace tera {

template<class ServerType>
class RpcClient {
public:
    RpcClient(const std::string& addr = "",
              int32_t wait_time = 0,
              int32_t rpc_timeout = 0,
              int32_t retry_times = 0)
    : m_rpc_client(m_client_options),
      m_wait_time(wait_time),
      m_rpc_timeout(rpc_timeout),
      m_retry_times(retry_times) {
        ResetClient(addr);
    }
    virtual ~RpcClient() {}

    std::string GetConnectAddr() const {
        return m_server_addr;
    }

protected:
    virtual void ResetClient(const std::string& server_addr) {
        if (m_server_addr == server_addr) {
            VLOG(10) << "address [" << server_addr << "] not be applied";
            return;
        }

        IpAddress ip_address(server_addr);
        if (!ip_address.IsValid()) {
            LOG(ERROR) << "invalid address: " << server_addr;
            return;
        }
        m_rpc_channel.reset(new sofa::pbrpc::RpcChannel(&m_rpc_client,
                                            ip_address.ToString(),
                                            m_channel_options));
        m_server_client.reset(new ServerType(m_rpc_channel.get()));

        m_server_addr = server_addr;
        VLOG(10) << "reset connected address to: " << server_addr;
    }

    template <class Request, class Response, class Callback>
    bool SendMessageWithRetry(void(ServerType::*func)(
                              google::protobuf::RpcController*,
                              const Request*, Response*, Callback*),
                              const Request* request, Response* response,
                              Callback* closure, const std::string& tips) {
        uint32_t wait_time = m_wait_time;
        uint32_t rpc_timeout = m_rpc_timeout;
        scoped_ptr<sofa::pbrpc::RpcController> rpc_controller(new sofa::pbrpc::RpcController());
        for (int32_t retry = 0; retry < m_retry_times; ++retry) {
            rpc_controller->SetTimeout(rpc_timeout);
            if (retry == 0 || PollAndResetServerAddr()) {

                CHECK_NOTNULL(m_server_client.get());
                (m_server_client.get()->*func)(rpc_controller.get(),
                                               request, response, closure);

                if (!rpc_controller->Failed()) {
                    if (IsRetryStatus(response->status()) && retry < m_retry_times - 1) {
                        LOG(WARNING) << tips << ": Server is busy [status = "
                            << StatusCodeToString(response->status())
                            << "], retry after " << rpc_timeout << " msec";
                        ThisThread::Sleep(rpc_timeout);
                    } else {
                        return true;
                    }
                } else {
                    LOG(ERROR) << "RpcRequest failed: " << tips
                        << ". Reason: " << rpc_controller->ErrorText()
                        << " (retry = " << retry << ")";
                    ThisThread::Sleep(wait_time);
                }
            } else {
                ThisThread::Sleep(wait_time);
            }
            rpc_controller->Reset();
            wait_time *= 2;
            rpc_timeout *= 2;
        }
        return false;
    }

    virtual bool PollAndResetServerAddr() {
        return true;
    }

    virtual bool IsRetryStatus(const StatusCode& status) {
        return false;
    }

private:
    sofa::pbrpc::RpcClientOptions m_client_options;
    sofa::pbrpc::RpcClient m_rpc_client;
    sofa::pbrpc::RpcChannelOptions m_channel_options;
    scoped_ptr<sofa::pbrpc::RpcChannel> m_rpc_channel;
    scoped_ptr<ServerType> m_server_client;

    std::string m_server_addr;
    int32_t m_wait_time;
    int32_t m_rpc_timeout;
    int32_t m_retry_times;
};

} // namespace tera

#endif // TERA_RPC_CLIENT_H
