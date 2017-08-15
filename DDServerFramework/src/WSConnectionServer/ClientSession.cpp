#include <unordered_map>
#include <algorithm>

#include "packet.h"
#include "WrapLog.h"
#include "ClientSessionMgr.h"

#include "WebSocketFormat.h"
#include "LogicServerSessionMgr.h"

#include "../../ServerConfig/ServerConfig.pb.h"
#include "./proto/LogicServerWithConnectionServer.pb.h"

#include "ClientSession.h"

extern ServerConfig::ConnectionServerConfig     connectionServerConfig;
extern WrapLog::PTR                             gDailyLogger;
extern WrapServer::PTR                          gServer;

ConnectionClientSession::ConnectionClientSession()
{
    mRuntimeID = -1;

    mRecvSerialID = 0;
    mSendSerialID = 0;

    mReconnecting = false;
}

ConnectionClientSession::~ConnectionClientSession()
{
    gDailyLogger->info("~ ConnectionClientSession : {}, {}", getSocketID(), getIP());
}

static int64_t MakeRuntimeID()
{
    union CLIENT_RUNTIME_ID
    {
        int64_t id;
        struct
        {
            int16_t serverID;
            int16_t incID;
            int32_t time;
        }humman;
    };

    static_assert(sizeof(union CLIENT_RUNTIME_ID) == sizeof(((CLIENT_RUNTIME_ID*)nullptr)->id), "");

    static std::atomic<int32_t> incRuntimeID = ATOMIC_VAR_INIT(0);

    CLIENT_RUNTIME_ID tmp;
    tmp.humman.serverID = connectionServerConfig.id();
    tmp.humman.incID = std::atomic_fetch_add(&incRuntimeID, 1);
    tmp.humman.time = static_cast<int32_t>(time(nullptr));

    return tmp.id;
}

void ConnectionClientSession::claimPrimaryServer()
{
    assert(mRuntimeID != -1);
    auto server = std::atomic_load(&mPrimaryServer);
    if (mRuntimeID != -1 && server == nullptr)
    {
        auto primaryID = LogicServerSessionMgr::ClaimPrimaryLogicServer();
        if (primaryID != -1)
        {
            std::atomic_store(&mPrimaryServer, LogicServerSessionMgr::FindPrimaryLogicServer(primaryID));
        }
        else
        {
            gDailyLogger->error("claim primary server failed of runtime id :{}, ip:{}", mRuntimeID, getIP());
        }
    }
}

void ConnectionClientSession::setPrimaryServerID(int id)
{
    gDailyLogger->info("set client {} primary server id:{}", mRuntimeID, id);

    LogicServerSession::PTR server = nullptr;
    if (id != -1)
    {
        server = LogicServerSessionMgr::FindPrimaryLogicServer(id);
        mReconnecting = false;
    }
    std::atomic_store(&mPrimaryServer, server);
}

void ConnectionClientSession::setSlaveServerID(int id)
{
    gDailyLogger->info("set client {} slave server id:{}", mRuntimeID, id);

    LogicServerSession::PTR server = nullptr;
    if (id != -1)
    {
        server = LogicServerSessionMgr::FindSlaveLogicServer(id);
    }
    std::atomic_store(&mSlaveServer, server);
}

int ConnectionClientSession::getPrimaryServerID() const
{
    auto server = std::atomic_load(&mPrimaryServer);
    return server ? server->getID() : -1;
}

int64_t ConnectionClientSession::getRuntimeID() const
{
    return mRuntimeID;
}

void ConnectionClientSession::notifyServerPlayerExist()
{
    gDailyLogger->info("ready notify client {} exist game", mRuntimeID);

    internalAgreement::CloseClientACK closeAck;
    closeAck.set_clientid(mRuntimeID);

    const int CLOSE_CLIENT_ACK_OP = 1513370829;

    // ���û���ҵ���������������,���������������,��ô��Ҫ�����з��������ͶϿ�(�����̨�Ѿ������ɹ�,Ȼ������ӶϿ�,��������������Ӿͻ�ʧ��)

    auto server = std::atomic_load(&mPrimaryServer);
    if (server != nullptr)
    {
        gDailyLogger->info("really notify client {} exist primary server", mRuntimeID);
        server->sendPB(CLOSE_CLIENT_ACK_OP, closeAck);
    }
    else if (mReconnecting)
    {
        auto primaryServers = LogicServerSessionMgr::GetAllPrimaryLogicServer();
        for (auto& v : primaryServers)
        {
            v.second->sendPB(CLOSE_CLIENT_ACK_OP, closeAck);
        }
    }

    server = std::atomic_load(&mSlaveServer);
    if (server != nullptr)
    {
        gDailyLogger->info("really notify client {} exist salve server", mRuntimeID);
        server->sendPB(CLOSE_CLIENT_ACK_OP, closeAck);
    }
    else if (mReconnecting)
    {
        auto slaveServers = LogicServerSessionMgr::GetAllSlaveLogicServer();
        for (auto& v : slaveServers)
        {
            v.second->sendPB(CLOSE_CLIENT_ACK_OP, closeAck);
        }
    }
}

void ConnectionClientSession::sendPBBinary(int32_t cmd, const char* data, size_t len)
{
    if (getEventLoop()->isInLoopThread())
    {
        helpSendPacket(cmd, data, len);
    }
    else
    {
        sendPBBinary(cmd, std::make_shared<std::string>(data, len));
    }
}

void ConnectionClientSession::sendPBBinary(int32_t cmd, std::shared_ptr<std::string>&& data)
{
    sendPBBinary(cmd, data);
}

void ConnectionClientSession::sendPBBinary(int32_t cmd, std::shared_ptr<std::string>& data)
{
    if (getEventLoop()->isInLoopThread())
    {
        helpSendPacket(cmd, data->c_str(), data->size());
    }
    else
    {
        auto sharedThis = std::static_pointer_cast<ConnectionClientSession>(shared_from_this());
        getEventLoop()->pushAsyncProc([=](){
            sharedThis->helpSendPacket(cmd, data->c_str(), data->size());
        });
    }
}

void ConnectionClientSession::helpSendPacket(uint32_t op, const char* data, size_t len)
{
    assert(getEventLoop()->isInLoopThread());

    char b[8 * 1024];
    BasePacketWriter packet(b, sizeof(b), true);
    packet.writeINT8('{');
    packet.writeUINT32(static_cast<uint32_t>(len + 14));
    packet.writeUINT32(op);
    packet.writeUINT32(mSendSerialID++);
    packet.writeBuffer(data, len);
    packet.writeINT8('}');

    auto frame = std::make_shared<std::string>();
    if (WebSocketFormat::wsFrameBuild(std::string(packet.getData(), packet.getPos()), *frame, WebSocketFormat::WebSocketFrameType::TEXT_FRAME))
    {
        sendPacket(frame);
    }
}

void ConnectionClientSession::procPacket(uint32_t op, const char* body, uint32_t bodyLen)
{
    const static int UPSTREAM_ACK_OP = 2210821449;
    const static std::vector<uint32_t> PRIMARY_OPS = { 24, 52, 74, 75 };

    mRecvSerialID++;

    if (op == 10000)
    {
        sendPBBinary(10000, "", 0);
        return;
    }

    gDailyLogger->info("recv op {} from id:{}", op, mRuntimeID);

    internalAgreement::UpstreamACK upstream;
    upstream.set_clientid(mRuntimeID);
    upstream.set_msgid(op);
    upstream.set_data(body, bodyLen);

    if (op == 91)
    {
        /*  ����������Ϊ��һ����Ϣ��    */
        if (mRecvSerialID == 1)
        {
            auto primaryServers = LogicServerSessionMgr::GetAllPrimaryLogicServer();
            for (auto& server : primaryServers)
            {
                server.second->sendPB(UPSTREAM_ACK_OP, upstream);
            }
        }

        mReconnecting = true;

        return;
    }

    auto primaryServer = std::atomic_load(&mPrimaryServer);
    auto slaveServer = std::atomic_load(&mSlaveServer);

    if (!primaryServer && !mReconnecting)
    {
        /*  ֻ�д�����û��������������������������������� */
        claimPrimaryServer();
    }

    if (std::find(PRIMARY_OPS.begin(), PRIMARY_OPS.end(), op) != PRIMARY_OPS.end())   /*  �ض���Ϣ�̶�ת��������������߼�������(TODO::������OPö�ٶ���) */
    {
        if (primaryServer != nullptr)
        {
            primaryServer->sendPB(UPSTREAM_ACK_OP, upstream);
        }
        else
        {
            gDailyLogger->error("primary server is nil");
        }
    }
    else
    {
        if (slaveServer != nullptr)
        {
            slaveServer->sendPB(UPSTREAM_ACK_OP, upstream);
        }
        else if (primaryServer != nullptr)
        {
            primaryServer->sendPB(UPSTREAM_ACK_OP, upstream);
        }
        else
        {
            gDailyLogger->error("no any server process this packet cmd:{} of client {}, ip is {}", op, mRuntimeID, getIP());
        }
    }
}

void ConnectionClientSession::onEnter()
{
    mRuntimeID = MakeRuntimeID();
    ClientSessionMgr::AddClientByRuntimeID(std::static_pointer_cast<ConnectionClientSession>(shared_from_this()), mRuntimeID);

    gDailyLogger->info("client enter, ip:{}, socket id :{}, runtime id:{}", getIP(), getSocketID(), mRuntimeID);
}

void ConnectionClientSession::onClose()
{
    gDailyLogger->info("client close, ip:{}, socket id :{}, runtime id:{}", getIP(), getSocketID(), getRuntimeID());
    if (mRuntimeID != -1)
    {
        auto sharedThis = std::static_pointer_cast<ConnectionClientSession>(shared_from_this());
        getEventLoop()->getTimerMgr()->addTimer(2 * 60 * 1000, [=](){
            sharedThis->notifyServerPlayerExist();
            ClientSessionMgr::EraseClientByRuntimeID(sharedThis->getRuntimeID());
            //����onClose���������EraseClientByRuntimeID������Ϊ�п����������һ�������ٴε�¼ʱ����ǰ�������ȴ�������Ͽ�,Ȼ����������ٴ���kick����ô����Ͽ����Ƴ�����ң����ҵȴ�2������֪ͨ�˳�
            //��ô������kick�߼�����޷�����֪ͨ�������(������޷���ʱ�����ٴε�¼)(��Ϊ�Ҳ�����Ҷ�����)
        });
    }
}