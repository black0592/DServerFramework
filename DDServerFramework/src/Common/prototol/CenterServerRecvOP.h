#ifndef _CENTERSERVER_RECV_OP_H
#define _CENTERSERVER_RECV_OP_H

enum class CENTER_SERVER_RECV_OP
{
    CENTERSERVER_RECV_OP_RPC,                       /*�յ��ڲ����������͵�RPC����*/
    CENTERSERVER_RECV_OP_PING,
    CENTERSERVER_RECV_OP_LOGICSERVER_LOGIN,         /*logic server��½*/
    CENTERSERVER_RECV_OP_USER,                      /*logic server�����û��Զ�����Ϣ*/
};

#endif