#include <set>
#include <fstream>
#include <string>
#include <iostream>
#include <sstream>

#include "WrapTCPService.h"
#include "Timer.h"
#include "ox_file.h"
#include "WrapLog.h"
#include "AutoConnectionServer.h"
#include "etcdclient.h"
#include "WrapJsonValue.h"
#include "app_status.h"
#include "ClientSession.h"
#include "LogicServerSession.h"
#include "../../ServerConfig/ServerConfig.pb.h"
#include "google/protobuf/util/json_util.h"

WrapLog::PTR                            gDailyLogger;

/*
    ���ӷ��������ڷ������ܹ��д���N������
    1�������ַ����etcd��Ⱥ. (���ڵľ�����������Է���etcd����ȡ���е����ӷ�������ַ��������һ�����Ʒ�����ͻ���)��
    2��������ͻ��˺��ڲ��߼������������ӣ�ת���ͻ��˺��ڲ�������֮���ͨ��
*/

int main()
{
    std::ifstream  connetionServerConfigFile("ServerConfig/ConnetionServerConfig.json");
    std::stringstream buffer;
    buffer << connetionServerConfigFile.rdbuf();

    ServerConfig::ConnectionServerConfig connectionServerConfig;
    google::protobuf::util::Status s = google::protobuf::util::JsonStringToMessage(buffer.str(), &connectionServerConfig);
    if (!s.ok())
    {
        std::cerr << "load config error:" << s.error_message() << std::endl;
        exit(-1);
    }

    ox_dir_create("logs");
    ox_dir_create("logs/ConnectionServer");

    gDailyLogger = std::make_shared<WrapLog>();
    gDailyLogger->setFile("", "logs/ConnectionServer/daily");

    auto service = std::make_shared<WrapTcpService>();
    
    service->startWorkThread(ox_getcpunum(), [](EventLoop::PTR) {
    });

    /*��������ͻ��˶˿�*/
    auto clientListener = ListenThread::Create();
    clientListener->startListen(connectionServerConfig.enableipv6(), connectionServerConfig.bindip(), connectionServerConfig.portforclient(), nullptr, nullptr, [=](sock fd) {
        WrapAddNetSession(service, fd, std::make_shared<ConnectionClientSession>(connectionServerConfig.id()), -1, 32 * 1024);
    });

    /*���������߼��������˿�*/
    auto logicServerListener = ListenThread::Create();
    logicServerListener->startListen(connectionServerConfig.enableipv6(), connectionServerConfig.bindip(), connectionServerConfig.portforlogicserver(), nullptr, nullptr, [=](sock fd) {
        WrapAddNetSession(service, fd, std::make_shared<LogicServerSession>(connectionServerConfig.id(), connectionServerConfig.logicserverloginpassword()), 10000, 32 * 1024 * 1024);
    });

    /*  ͬ��etcd  */
    std::map<std::string, std::string> etcdKV;

    etcdKV["value"] = buffer.str();
    etcdKV["ttl"] = std::to_string(15); /*���ʱ��Ϊ15��*/

    while (true)
    {
        if (app_kbhit())
        {
            std::string input;
            std::getline(std::cin, input);
            gDailyLogger->warn("console input {}", input);

            if (input == "quit")
            {
                break;
            }
        }

        for (auto& etcd : connectionServerConfig.etcdservers())
        {
            if (!etcdSet(etcd.ip(), etcd.port(), std::string("ConnectionServerList/") + std::to_string(connectionServerConfig.id()), etcdKV, 5000).getBody().empty())
            {
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5000));   /*5s ����һ��*/
    }

    clientListener->closeListenThread();
    logicServerListener->closeListenThread();
    service->getService()->closeListenThread();
    service->getService()->stopWorkerThread();
    gDailyLogger->stop();

    return 0;
}