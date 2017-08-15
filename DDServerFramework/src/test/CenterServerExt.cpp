#include <string>
#include "../CenterServer/CenterServerSession.h"
#include "WrapLog.h"
#include "CenterServerExtRecvOP.h"

extern WrapLog::PTR gDailyLogger;

//TODO::������������RPCҲ��Ҫ����дһ��,��Ҫ���ǳ�����ȡ����

static int add(int a, int b)
{
    //CenterServerRPCMgr::getRpcFromer()Ϊ�����߻Ự����
    return a + b;
}

static void addNoneRet(int a, int b, dodo::rpc::RpcRequestInfo reqInfo)
{
    // ���dodo::RpcRequestInfo reqInfo�β�(��Ӱ������ߵ���)
    // ���ﱾ����������(������������Ϊvoid),��RPC�����Ǿ��з���ֵ�����
    // ��������Ҫ���������첽����֮��(ͨ��reqInfo)���ܷ������ݸ������ߵ����
    // Ʃ��:
    /*
        auto caller = CenterServerRPCMgr::getRpcFromer();
        redis->get("k", [caller, reqInfo](const std::string& value){
            caller->reply(reqInfo, value);
        });
    */

    /*
        //�ͻ���:
        centerServerConnectionRpc->call("test", 1, 2, [](const std::string& value){

        });
    */
}

void initCenterServerExt()
{
    CenterServerRPCMgr::def("test", [](int a, int b){
        return a + b;
    });

    CenterServerRPCMgr::def("testNoneRet", [](int a, int b){
    });

    CenterServerRPCMgr::def("add", add);

    CenterServerRPCMgr::def("addNoneRet", addNoneRet);

    /*���������ڲ���������rpc����,������Ϊ:gCenterServerSessionRpcFromer*/
    CenterServerSessionGlobalData::getCenterServerSessionRpc()->def("testrpc", [](const std::string& a, int b, dodo::rpc::RpcRequestInfo info){
        gDailyLogger->info("rpc handle: {} : {}", a, b);
    });

    /*���������ڲ����������͵�OP��Ϣ*/
    CenterServerSessionGlobalData::registerUserMsgHandle(static_cast<PACKET_OP_TYPE>(CENTER_SERVER_EXT_RECV_OP::CENTER_SERVER_EXT_RECV_OP_TEST),
        [](CenterServerSession::PTR&, ReadPacket& rp){
            std::string a = rp.readBinary();
            int b = rp.readINT32();
            gDailyLogger->info("test op : {} : {}", a, b);
        });
}