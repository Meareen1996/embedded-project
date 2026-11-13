#include <stdio.h>
#include <mosquitto.h>
#include <stdbool.h>

// 添加connect回调函数
static int connact_rc = -1; // connack_rc = -1:表示还没收到回应
static void on_connect(struct mosquitto *client, void *userdata, int rc)
// rc==0:服务器接受连接；其他值表示拒绝原因
{
    (void)client;
    (void)userdata;
    connact_rc = rc;
    printf("收到服务器连接回应，rc=%d\n", rc);
}

// 发布回调
static bool publish_done = false;
static void on_publish(struct mosquitto *client, void *userdata, int mid)
{
    (void)client;
    (void)userdata;
    publish_done = true;
    printf("消息发布已确认，mid=%d\n", mid);
}

int main(void)
{
    // 初始化库
    int ret = mosquitto_lib_init();
    if (ret != MOSQ_ERR_SUCCESS) {
        printf("MQTT库初始化失败：%s\n", mosquitto_strerror(ret));
        // 这里的mosquitto_strerror(ret)把库的错误吗转换成说明文字，不使用perror()
        return 1;
    }
    // 成功后打印提示，并清理库
    printf("MQTT库初始化成功\n");
    // 初始化之后，创建客户端
    struct mosquitto *client = mosquitto_new("board01", true, NULL);

    // client是一个指针变量，保存库创建的客户端对象的地址
    //  这次的返回规则是：
    //  成功：返回对象地址，保存在client中
    //  失败：返回NULL，并设置errno

    if (client == NULL) {
        perror("MQTT客户端创建失败");
        mosquitto_lib_cleanup();
        return 1;
    }

    printf("MQTT客户端创建成功\n");

    // 创建客户端并检查成功后，我们需要让库去处理连接回调
    mosquitto_connect_callback_set(client, on_connect);

    ret = mosquitto_connect(client, "127.0.0.1", 1883, 60);

    if (ret != MOSQ_ERR_SUCCESS) {
        printf("连接调用失败：%s\n", mosquitto_strerror(ret));
        mosquitto_strerror(ret); // 打印错误原因
        mosquitto_destroy(client);
        mosquitto_lib_cleanup();
        return 1;
    }

    printf("连接调用成功\n");

    // 连接调用成功后，加入网络处理循环
    for (int i = 0; i < 10 && connact_rc == -1; i++) {
        ret = mosquitto_loop(client, 1000, 1);
        if (ret != MOSQ_ERR_SUCCESS) {
            printf("网络处理失败：%s\n", mosquitto_strerror(ret));
            break;
        }
    }

    if (connact_rc != 0 || ret != MOSQ_ERR_SUCCESS) {
        printf("未获得成功的连接确认\n");
        mosquitto_destroy(client);
        mosquitto_lib_cleanup();
        return 1;
    }

    printf("MQTT连接已确认成功\n");
    mosquitto_publish_callback_set(client, on_publish);

    int mid = 0;
    // client:使用哪个客户端
    // &mid:让库把这条消息填进mid
    // "learn/board01/test":发布主题
    // 5:内容长度，‘hello’是5个字节，不包含结尾的\0
    // "hello":消息内容
    // 1:使用Qos 1，需要Broker返回确认
    // false: 不将它设置为保留消息
    // 测试json数据格式

    int temperature = 26;
    int humidity = 40;
    // 把原来的payload定义替换为缓冲区和格式化操作

    char payload[128];

    int length = snprintf(payload, sizeof(payload), "{\"temperature\":%d,\"humidity\":%d}",
                          temperature, humidity);

    if (length < 0 || length >= (int)sizeof(payload)) {
        printf("JSON生成失败或缓冲区不足\n");
        mosquitto_destroy(client);
        mosquitto_lib_cleanup();
        return 1;
    }

    printf("准备发送：%s\n", payload);
    ret = mosquitto_publish(client, &mid, "learn/board01/test", length, payload, 1, false);

    for (int i = 1; i < 10 && !publish_done; i++) {
        ret = mosquitto_loop(client, 1000, 1);
        if (ret != MOSQ_ERR_SUCCESS) {
            printf("网络处理失败：%s\n", mosquitto_strerror(ret));
        }
    }

    if (!publish_done || ret != MOSQ_ERR_SUCCESS) {
        printf("未发布成功\n");
        mosquitto_disconnect(client);
        mosquitto_destroy(client);
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_disconnect(client);
    mosquitto_destroy(client);
    mosquitto_lib_cleanup();

    return 0;
}
