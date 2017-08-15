#include <unordered_map>

#include "packet.h"
#include "WrapLog.h"
#include "ClientSessionMgr.h"
#include "ConnectionServerSendOP.h"
#include "WebSocketFormat.h"
#include "LogicServerSessionMgr.h"

#include "../../ServerConfig/ServerConfig.pb.h"

#include "ClientSession.h"

extern WrapLog::PTR                             gDailyLogger;
static std::atomic<int32_t> incRuntimeID = ATOMIC_VAR_INIT(0);

ConnectionClientSession::ConnectionClientSession(int32_t connectionServerID) : mConnectionServerID(connectionServerID)
{
    mRuntimeID = -1;
    mPrimaryServerID = -1;
    mSlaveServerID = -1;

    mRecvSerialID = 0;
    mSendSerialID = 0;
}

ConnectionClientSession::~ConnectionClientSession()
{
}

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

void ConnectionClientSession::claimRuntimeID()
{
    if (mRuntimeID != -1)
    {
        return;
    }

    static_assert(sizeof(union CLIENT_RUNTIME_ID) == sizeof(((CLIENT_RUNTIME_ID*)nullptr)->id), "");

    CLIENT_RUNTIME_ID tmp;
    tmp.humman.serverID = mConnectionServerID;
    tmp.humman.incID = std::atomic_fetch_add(&incRuntimeID, 1);
    tmp.humman.time = static_cast<int32_t>(time(nullptr));

    mRuntimeID = tmp.id;
    ClientSessionMgr::AddClientByRuntimeID(std::static_pointer_cast<ConnectionClientSession>(shared_from_this()), mRuntimeID);

    BigPacket packet(static_cast<PACKET_OP_TYPE>(CONNECTION_SERVER_SEND_OP::CONNECTION_SERVER_SEND_LOGICSERVER_INIT_CLIENTMIRROR));
    packet.writeINT64(getSocketID());
    packet.writeINT64(mRuntimeID);

    mPrimaryServer = LogicServerSessionMgr::FindPrimaryLogicServer(mPrimaryServerID);
    if (mPrimaryServer != nullptr)
    {
        mPrimaryServer->sendPacket(packet.getData(), packet.getLen());
    }
}

void ConnectionClientSession::claimPrimaryServer()
{
    if (mPrimaryServer != nullptr)
    {
        return;
    }

    mPrimaryServerID = LogicServerSessionMgr::ClaimPrimaryLogicServer();
    if (mPrimaryServerID != -1)
    {
        mPrimaryServer = LogicServerSessionMgr::FindPrimaryLogicServer(mPrimaryServerID);
    }
    else
    {
        gDailyLogger->error("�������߼�������ʧ��");
    }
}

int ConnectionClientSession::getPrimaryServerID() const
{
    return mPrimaryServerID;
}

int64_t ConnectionClientSession::getRuntimeID() const
{
    return mRuntimeID;
}

void ConnectionClientSession::setSlaveServerID(int id)
{
    gDailyLogger->info("set client {} slave server id:{}", mRuntimeID, id);
    mSlaveServerID = id;
}

void ConnectionClientSession::procPacket(PACKET_OP_TYPE op, const char* body, PACKET_LEN_TYPE bodyLen)
{
    if (mPrimaryServerID == -1)
    {
        claimPrimaryServer();
    }

    if (mRuntimeID == -1)
    {
        claimRuntimeID();
    }

    if (mSlaveServerID != -1)
    {
        if (mSlaveServer == nullptr)
        {
            mSlaveServer = LogicServerSessionMgr::FindSlaveLogicServer(mSlaveServerID);
        }
    }
    else
    {
        mSlaveServer = nullptr;
    }

    if (mPrimaryServerID != -1)
    {
        BigPacket packet(static_cast<PACKET_OP_TYPE>(CONNECTION_SERVER_SEND_OP::CONNECTION_SERVER_SEND_LOGICSERVER_FROMCLIENT));
        packet.writeINT64(mRuntimeID);
        packet.writeBinary(body- PACKET_HEAD_LEN, bodyLen+ PACKET_HEAD_LEN);
        packet.getLen();

        if (mSlaveServer != nullptr)
        {
            mSlaveServer->sendPacket(packet.getData(), packet.getLen());
        }
        else if (mPrimaryServer != nullptr)
        {
            mPrimaryServer->sendPacket(packet.getData(), packet.getLen());
        }
    }
}

void ConnectionClientSession::onEnter()
{
    gDailyLogger->warn("client enter, ip:{}, socket id :{}", getIP(), getSocketID());
}

void ConnectionClientSession::onClose()
{
    gDailyLogger->warn("client close, ip:{}, socket id :{}, runtime id:{}", getIP(), getSocketID(), getRuntimeID());
    if (mRuntimeID != -1)
    {
        ClientSessionMgr::EraseClientByRuntimeID(mRuntimeID);

        TinyPacket sp(static_cast<PACKET_OP_TYPE>(CONNECTION_SERVER_SEND_OP::CONNECTION_SERVER_SEND_LOGICSERVER_DESTROY_CLIENT));
        sp.writeINT64(mRuntimeID);
        sp.getLen();

        if (mPrimaryServer != nullptr)
        {
            mPrimaryServer->sendPacket(sp.getData(), sp.getLen());
        }
        if (mSlaveServer != nullptr)
        {
            mSlaveServer->sendPacket(sp.getData(), sp.getLen());
        }

        mRuntimeID = -1;
    }
}