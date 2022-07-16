#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <iostream>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>
#include <functional>
using namespace std;
#include "Connection.h"

class ConnectionPool
{
public:
    //对外接口

    //外界获取线程池对象的接口
    static ConnectionPool* getConnectionPool();

    //获取连接
    shared_ptr<Connection> getConnection();

private:
    //私有方法

    //构造函数私有化
    ConnectionPool();

    //加载配置文件
    bool loadConfigFile();

    //生产者，专门负责生产连接，让其独立运行在一个单独的线程中
    void produceConnectionTask();

    //巡视连接队列，回收空闲连接
    void scanConnectionQue();

private:
    //数据库连接的信息
    string ip_;       //服务器ip地址
    int port_;             //服务器mysql端口号，默认 3306
    string username_; //用户名
    string password_; //密码
    string dbname_;   //数据库名称

    //连接池配置信息
    int initSize_;                          //初始连接量
    int maxSize_;                           //最大连接量
    int maxIdleTime_;                       //连接最大空闲时间，超过则进行回收
    int maxTimeout_;                        //获取连接等待的超时时间，超时则返回一个空指针
    atomic_int connectionTotalConuts_; /*已经生产的连接的总数量
    每生产一个连接加1，回收一个减1，采用原子类型保证线程安全
    用于与连接队列大小比较，回收多余连接*/

    //连接队列信息
    queue<Connection *> connectionQue_; /*连接队列，存放指向连接的指针
    queue先进先出，队尾插入元素，队头删除元素，所以获取连接时front()获取队头元素然后pop()删除即可*/
    mutex queMutex_;                    //用于连接队列增删操纵的互斥锁，保证线程安全
    condition_variable cv_;             //用于线程之间通信的条件变量
};