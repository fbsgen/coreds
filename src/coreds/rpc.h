#pragma once

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>

#include <brynet/net/http/HttpService.h>
#include <brynet/net/http/HttpFormat.h>

#include "util.h"

namespace coreds {
namespace rpc {

const char* const MALFORMED_MESSAGE = "Malformed message.";

const char* extractJson(std::string& body)
{
    // remove suffix: ]
    body[body.size() - 1] = '\0';
    
    // remove prefix: +[0,
    return body.data() + 4;
}

const char* extractMsg(std::string& body)
{
    size_t last_dquote = body.rfind('"'),
            expect_dquote = body.size() - 3,
            colon;
    
    if (last_dquote != expect_dquote ||
            std::string::npos == (colon = body.find(':')) ||
            // prevent empty string
            2 > (last_dquote - colon + 1) ||
            '"' != body[colon + 1] ||
            '"' == body[colon + 2])
    {
        return MALFORMED_MESSAGE;
    }
    
    // remove suffix: "}]
    body[expect_dquote] = '\0';
    
    // :"
    return body.data() + colon + 2;
}

const bool parseJson(std::string& body,
        const char* root, flatbuffers::Parser& parser, std::string& errmsg)
{
    bool ok = false;
    if (3 > body.size())
    {
        errmsg.assign(MALFORMED_MESSAGE);
    }
    else if ('-' == body[0])
    {
        errmsg.assign(body.data() + 1, body.size() - 1);
    }
    else if ('+' != body[0] || '[' != body[1])
    {
        errmsg.assign(MALFORMED_MESSAGE);
    }
    else if ('0' != body[2])
    {
        errmsg.assign(rpc::extractMsg(body));
    }
    else if (!root || (parser.SetRootType(root) && parser.ParseJson(rpc::extractJson(body), true)))
    {
        ok = true;
    }
    else
    {
        errmsg.assign(MALFORMED_MESSAGE);
    }
    
    return ok;
}

/*
bool fetchInitialTodos(UrlRequest& req, flatbuffers::Parser& parser, std::string& errmsg)
{
    req.uri("/todo/user/Todo/list")
        .method("POST")
        .addHeader("Content-Type: application/json")
        .body(R"({"1":true,"2":31})");
    
    auto res = std::move(req.perform());
    auto body = res.body();
    
    if (200 != res.statusCode())
    {
        errmsg.assign("Request failed.");
        return false;
    }
    
    if ('+' != body[0])
    {
        errmsg.assign(body.c_str() + 1);
        return false;
    }
    
    if (!parser.SetRootType("Todo_PList") || !parser.ParseJson(extractJson(body), true))
    {
        errmsg.assign("Malformed message.");
        return false;
    }
    
    return true;
}
*/

struct Config
{
    const char* host; // ip address
    const int port;
    const bool secure; // ssl
    const char* hostname; // override host that appears in the request header
    
    Config(const char* host, const int port, const bool secure, const char* hostname = nullptr):
        host(host), port(port), secure(secure), hostname(hostname) {}
        
    static const Config parseFrom(char* endpoint, char* hostname = nullptr, int default_nonsecure_port = 0)
    {
        int port = 0;
        bool secure = false;
        const char* host = coreds::util::resolveEndpoint(endpoint, &port, &secure);
        
        if (port != 0)
        {
            // provided
        }
        else if (secure)
        {
            port = 443;
        }
        else if (default_nonsecure_port != 0 && host == coreds::util::DEFAULT_HOST)
        {
            port = default_nonsecure_port;
        }
        else
        {
            port = 80;
        }
        
        return { host, port, secure, hostname };
    }
};

struct Base : brynet::NonCopyable
{
    const Config config;
    const std::string host;
    flatbuffers::Parser parser;
    std::string errmsg;
    std::string req_host;
    
private:
    std::string req_buf;
    brynet::net::WrapTcpService service;
    bool started{ false };
    
protected:
    int fd{ SOCKET_ERROR };
    
    Base(const Config config) : config(config), host(config.host)
    {
        if (config.port == 80 && !config.secure)
        {
            req_host.assign(config.hostname ? config.hostname : host);
        }
        else if (config.hostname)
        {
            req_host.assign(config.hostname);
        }
        else
        {
            req_host += host;
            req_host += ':';
            req_host += std::to_string(config.port);
        }
        
        if (config.secure)
            SSL_library_init();
    }
    
    void post(const brynet::net::HttpSession::PTR& session,
            const std::string& uri, const std::string& body)
    {
        /*
        brynet::net::HttpRequest req;
        
        req.setMethod(brynet::net::HttpRequest::HTTP_METHOD::HTTP_METHOD_POST);
        req.setUrl(uri);
        req.setHost(req_host);
        req.setContentType("application/json");
        req.setBody(body);
        
        const std::string payload = req.getResult();
        session->send(payload.data(), payload.size());
        */
        
        req_buf.assign("POST ");
        req_buf += uri;
        req_buf += " HTTP/1.1\r\nHost: ";
        req_buf += req_host;
        req_buf += "\r\nContent-Type: application/json\r\nContent-Length: ";
        req_buf += std::to_string(body.size());
        req_buf += "\r\n\r\n";
        req_buf += body;
        
        session->send(req_buf.data(), req_buf.size());
    }
    
    virtual void onLoop(const brynet::net::EventLoop::PTR& loop) = 0;
    std::function<void (const brynet::net::EventLoop::PTR& loop)> $onLoop{
        std::bind(&Base::onLoop, this, std::placeholders::_1)
    };
    
    void start()
    {
        if (!started)
        {
            started = true;
            service.startWorkThread(1, $onLoop);
        }
    }
    
    virtual void onHttpData(const brynet::net::HTTPParser& httpParser, const brynet::net::HttpSession::PTR& session) = 0;
    std::function<void (const brynet::net::HTTPParser& httpParser, const brynet::net::HttpSession::PTR& session)> $onHttpData{
        std::bind(&Base::onHttpData, this, std::placeholders::_1, std::placeholders::_2)
    };
    
    virtual void onHttpClose(const brynet::net::HttpSession::PTR& httpSession) = 0;
    std::function<void (const brynet::net::HttpSession::PTR& httpSession)> $onHttpClose{
        std::bind(&Base::onHttpClose, this, std::placeholders::_1)
    };
    
    virtual void onHttpOpen(const brynet::net::HttpSession::PTR& httpSession) = 0;
    
private:
    void setupHttp(const brynet::net::HttpSession::PTR& httpSession)
    {
        httpSession->setCloseCallback($onHttpClose);
        httpSession->setHttpCallback($onHttpData);
        onHttpOpen(httpSession);
    }
    std::function<void (const brynet::net::HttpSession::PTR& httpSession)> $setupHttp{
        std::bind(&Base::setupHttp, this, std::placeholders::_1)
    };
    
    void onTcpSession(const brynet::net::TCPSession::PTR& tcpSession)
    {
        brynet::net::HttpService::setup(tcpSession, $setupHttp);
    }
    std::function<void (const brynet::net::TCPSession::PTR& tcpSession)> $onTcpSession{
        std::bind(&Base::onTcpSession, this, std::placeholders::_1)
    };
    
protected:
    /**
     * Returns true if the connection is successful or if already connected when not forced.
     */
    bool connect(bool force = false)
    {
        bool ret = !force && SOCKET_ERROR != fd;
        
        if (!ret && SOCKET_ERROR != (fd = brynet::net::base::Connect(false, host, config.port)))
        {
            auto socket = brynet::net::TcpSocket::Create(fd, false);
            service.addSession(std::move(socket), $onTcpSession, config.secure, nullptr, 1024 * 1024, false);
            ret = true;
        }
        
        return ret;
    }
    std::function<void ()> $connect{
        std::bind(&Base::connect, this, false)
    };
    
public:
    bool isConnected()
    {
        return SOCKET_ERROR != fd;
    }
    
    void queue(std::function<void()> fn)
    {
        service.getService()->getRandomEventLoop()->pushAsyncProc(fn);
    }
};

} // rpc
} // coreds
