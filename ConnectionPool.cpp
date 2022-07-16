#include "ConnectionPool.h"
#include "PoolLogger.h"

//加载配置文件
bool ConnectionPool::loadConfigFile()
{
    FILE *pf = fopen("/home/liuyu/project/ConnectionPool/PoolSetting.cnf", "r");
    if (pf == nullptr)
    {
        LOG("ConfigFile open failed");
        return false;
    }

    while (!feof(pf))
	{
		char line[1024] = {0};
		fgets(line, 1024, pf);
		string str = line;
		int idx = str.find('=', 0);
		if (idx < 0) // 无效的配置项
		{
			continue;
		}

		// password=123456\n
		int endidx = str.find('\n', idx);
		string key = str.substr(0, idx);
		string value = str.substr(idx + 1, endidx - idx - 1);

        if (key == "ip")
        {
            ip_ = value;
        }
        else if (key == "port")
        {
            port_ = atoi(value.c_str());
        }
        else if (key == "username")
        {
            username_ = value;
        }
        else if (key == "password")
        {
            password_ = value;
        }
        else if (key == "dbname")
        {
            dbname_ = value;
        }
        else if (key == "initSize")
        {
            initSize_ = atoi(value.c_str());
        }
        else if (key == "maxSize")
        {
            maxSize_ = atoi(value.c_str());
        }
        else if (key == "maxIdleTime")
        {
            maxIdleTime_ = atoi(value.c_str());
        }
        else if (key == "maxTimeout")
        {
            maxTimeout_ = atoi(value.c_str());
        }
    }

    return true;
}

//构造函数私有化
ConnectionPool::ConnectionPool()
{
    if (!loadConfigFile())
    {
        return;
    }

    //创建初始连接量的连接
    for (int i = 0; i < initSize_; i++)
    {
        Connection *p = new (Connection);
        p->connect(ip_, port_, username_, password_, dbname_); //连接数据库
        p->refreshAliveTime();                                 //刷新计时器
        connectionQue_.push(p);                                //加入队列
        connectionTotalConuts_++;                              //总数加一
    }

    //开启一个独立的线程用于生产连接
    std::thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
    produce.detach();

    //开启一个独立的线程用于巡视回收空闲连接
    std::thread scanner(std::bind(&ConnectionPool::scanConnectionQue, this));
    scanner.detach();
}

//外界获取线程池对象的接口
ConnectionPool *ConnectionPool::getConnectionPool()
{
    static ConnectionPool pool;
    return &pool;
}

//生产者，专门负责生产连接，让其独立运行在一个单独的线程中
void ConnectionPool::produceConnectionTask()
{
    //开启一个循环不断生产，利用条件变量阻塞等待
    for (;;)
    {
        //上锁
        unique_lock<mutex> lock(queMutex_);
        while (!connectionQue_.empty())
        {
            cv_.wait(lock); //释放锁，阻塞在此等待被唤醒
        }

        //为空时总连接量不超限额就生产新连接
        if (connectionTotalConuts_ < maxSize_)
        {
            Connection *p = new (Connection);
            p->connect(ip_, port_, username_, password_, dbname_); //连接数据库
            p->refreshAliveTime();                                 //刷新计时器
            connectionQue_.push(p);                                //加入队列
            connectionTotalConuts_++;                              //总数加一
        }

        cv_.notify_all(); //通知消费者消费
    }
}

//巡视连接队列，回收空闲连接，让其独立运行在一个单独的线程中
void ConnectionPool::scanConnectionQue()
{
    for (;;)
    {
        this_thread::sleep_for(std::chrono::seconds(maxTimeout_));
        unique_lock<mutex> lock(queMutex_);
        while (connectionTotalConuts_ > initSize_)
        {
            Connection *p = connectionQue_.front();
            if (p->getAliveeTime() > (maxTimeout_ * 1000))
            {
                connectionQue_.pop();
                connectionTotalConuts_--;
                delete p;
            }
            else
            {
                break;
            }
        }
    }
}

//获取连接
shared_ptr<Connection> ConnectionPool::getConnection()
{
    //上锁
    unique_lock<mutex> lock(queMutex_);
    //判断队空
    while (connectionQue_.empty())
    {
        //等待超时则再次判断是否为空，为空则返回nullptr
        if (cv_status::timeout == cv_.wait_for(lock, chrono::milliseconds(maxTimeout_)))
        {
            if (connectionQue_.empty())
            {
                LOG("getConnection Timeout\n");
                return nullptr;
            }
        }
    }

    /*从队头获取连接，因为queue删除是头删*/
    shared_ptr<Connection> sp(connectionQue_.front(),
                              [&](Connection *pcon)
                              {
                                  /*析构操作为将结束任务的连接归还到队列中，所以需要上锁*/
                                  unique_lock<mutex> lock(queMutex_);
                                  pcon->refreshAliveTime();  /*刷新计时器*/
                                  connectionQue_.push(pcon); /*归还到队列中*/
                              });

    connectionQue_.pop(); /*连接出队*/
    cv_.notify_all();     /*通知生产者*/

    return sp;
}
