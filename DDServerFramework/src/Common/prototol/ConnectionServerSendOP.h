#ifndef _CONNECTIONSERVER_2_LOGICSERVER_H
#define _CONNECTIONSERVER_2_LOGICSERVER_H

/*  ���ӷ��������͸��߼�����������Ϣ��   */
enum class CONNECTION_SERVER_SEND_OP
{
    CONNECTION_SERVER_SEND_PONG,
    //����user msg �д��� (����ping��CONNECTION_SERVER_SEND_LOGICSERVER_CLIENT_RECONNECT,    /*  ���ӷ�����֪ͨ�߼�������ĳ�ͻ��˶�����������   */
    CONNECTION_SERVER_SEND_LOGICSERVER_RECVCSID,            /*  ���ӷ�����������id���͸�Logic������    */
    CONNECTION_SERVER_SEND_LOGICSERVER_INIT_CLIENTMIRROR,   /*  ֪ͨLogic��������ʼ��ĳ�ͻ���session  */
    CONNECTION_SERVER_SEND_LOGICSERVER_DESTROY_CLIENT,      /*  ֪ͨ�߼�������ǿ��ɾ���ͻ���   */
    CONNECTION_SERVER_SEND_LOGICSERVER_FROMCLIENT,          /*  ���ӷ�����ת���ͻ��˵���Ϣ�����߼������� */
};

#endif