//内核模块必需的头文件 
#include <linux/module.h>          // 模块基础支持
#include <linux/platform_device.h> // 平台设备驱动支持
#include <linux/gpio/consumer.h>   // GPIO描述符接口
#include <linux/interrupt.h>       // 中断处理相关
#include <linux/input.h>           // 输入子系统
#include <linux/of.h>              // 设备树支持

/* 驱动定义宏 */
#define DRIVER_NAME "gpio_keys_agn" // 驱动名称,与设备树进行匹配
#define KEY_NUM     2               // 按键数量

// 定义按键数据结构 
struct agn_key {
    struct gpio_desc *gpiod;   // GPIO描述符指针 
    int irq;                   // 中断号 
    int keycode;               // 键值 
    struct input_dev *input;   // 输入设备指针 
};

//驱动全局数据结构 
struct agn_key_drvdata {
    struct agn_key keys[KEY_NUM]; // 存储所有按键实例的数组
};

//中断处理函数  核心
static irqreturn_t agn_key_interrupt(int irq, void *dev_id)
{
    struct agn_key *key = dev_id; // 通过dev_id获取对应的按键实例
    int state = gpiod_get_value(key->gpiod); // 读取当前GPIO电平
    /* 
    上报按键事件：
    GPIO配置为低电平有效 当按键按下时实际电平为0，此时!state=1表示按键按下，当按键释放时电平恢复为1，!state=0表示按键释放
     */
    input_report_key(key->input, key->keycode, !state); 
    input_sync(key->input); // 同步输入事件
    return IRQ_HANDLED;     // 中断已处理
}

// 驱动的探测函数（设备初始化入口）
static int agn_key_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;         // 获取设备结构体
    struct agn_key_drvdata *ddata;           // 驱动数据指针
    struct input_dev *input;                 // 输入设备指针
    int ret, i;                              // 临时变量
    u32 keycodes[KEY_NUM] = {KEY_1, KEY_2};  // 自定义键值数组

    //分配驱动数据结构（自动内存管理)
    ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
    if (!ddata){
        return -ENOMEM; 
    }
        
    //遍历初始化按键 
    for (i = 0; i < KEY_NUM; i++){
        //获取GPIO资源（自动解析设备树中的key_gpios属性）
        ddata->keys[i].gpiod = devm_gpiod_get_index(dev, "key", i, GPIOD_IN);
        if (IS_ERR(ddata->keys[i].gpiod)){
            dev_err(dev, "Failed to get gpio %d\n", i);
            return PTR_ERR(ddata->keys[i].gpiod);
        }

        // 将GPIO映射为中断号 
        ddata->keys[i].irq = gpiod_to_irq(ddata->keys[i].gpiod);
        
        //注册中断处理函数 
        ret = devm_request_irq(dev, ddata->keys[i].irq, agn_key_interrupt,
                   IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, // 双边沿触发
                   dev_name(dev), &ddata->keys[i]);             // 传递按键实例指针
        if (ret) {
            dev_err(dev, "Failed to request IRQ %d\n", i);
            return ret;
        }

        //分配输入设备结构体 
        input = devm_input_allocate_device(dev);
        if (!input)
            return -ENOMEM;

        //初始化输入设备参数 
        input->name = "AGN GPIO Keys";       // 设备名称
        input->id.bustype = BUS_HOST;        // 总线类型
        input->dev.parent = dev;             // 设置父设备

        //设置事件类型和键值
        __set_bit(EV_KEY, input->evbit);     // 启用按键事件
        __set_bit(keycodes[i], input->keybit); // 设置支持的键值

        //保存配置到数据结构 
        ddata->keys[i].input = input;
        ddata->keys[i].keycode = keycodes[i];

        //注册输入设备到内核
        ret = input_register_device(input);
        if (ret) {
            dev_err(dev, "Failed to register input device\n");
            return ret;
        }
    }

    //将驱动数据关联到平台设备
    platform_set_drvdata(pdev, ddata);
    return 0; // 初始化成功
}

/* 设备树匹配表 */
static const struct of_device_id agn_key_of_match[] = {

    { .compatible = "agn.gpio_key", }, // 匹配设备树中的compatible属性
    { },
};
MODULE_DEVICE_TABLE(of, agn_key_of_match); // 生成设备表用于自动加载驱动

/* 平台驱动结构体 */
static struct platform_driver agn_key_driver = {
    .probe = agn_key_probe, // 指向探测函数
    .driver = {
        .name = DRIVER_NAME,          // 驱动名称
        .of_match_table = agn_key_of_match, // 关联设备树匹配表
    },
};

//模块初始化
module_platform_driver(agn_key_driver); // 自动注册/注销平台驱动

//模块元信息 
MODULE_LICENSE("GPL");                  // 必须声明GPL协议
MODULE_AUTHOR("");             // 作者信息
MODULE_DESCRIPTION("AGN GPIO Key Driver"); // 驱动描述