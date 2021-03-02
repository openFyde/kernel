/*
 * hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
//#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "snd-soc-aic3106.h"

/****************************************************************************/
/*                                                                          */
/*              写数据到寄存器                                              */
/*                                                                          */
/****************************************************************************/
static int I2CRegWrite(struct i2c_client *client, unsigned char regAddr,
                   unsigned char regData)
{
   char buf[2];
   int ret = -1;

   buf[0]=regAddr;
   buf[1]=regData;
   ret = i2c_master_send(client, buf, 2);
   if(ret<0)
       pr_err("aic3106_write_reg_byte: master send fail !\n");

     return ret;
}

static unsigned char I2CRegRead(struct i2c_client * client, unsigned char regAddr)
{
   int ret = -1;
   unsigned char buf[2];

   buf[0]=regAddr;
   ret=i2c_master_send(client, buf, 1);
   if(ret<0)
       pr_err("aic3106_read_reg: master send fail !\n");

   ret=i2c_master_recv(client, buf, 1);
   if(ret<0){
       pr_err("aic3106_read_reg: master recv fail !\n");
       return ret;
   }
 
        return buf[0];
}

/****************************************************************************/
/*                                                                          */
/*              IIC \u6e05\u9664\u5bc4\u5b58\u5668\u7684\u67d0\u4e00bit                                     */
/*                                                                          */
/****************************************************************************/
static void I2CRegBitClr(struct i2c_client *client, unsigned char regAddr,
                    unsigned char bitMask)
{
         unsigned char val;

   val = I2CRegRead(client, regAddr);
   val = val & ~bitMask;
   
   I2CRegWrite(client, regAddr, val);
}

/* ***************************************************************************/
// *  功能   复位AIC3106                                                                      
// *
// *  参数    AIC3106  Slave Address                                                   
// *
//*  返回    无                                                    
//*
/****************************************************************************/
static void AIC31Reset(struct i2c_client *client)
{
   // 选择 Page 0
    I2CRegWrite(client, AIC31_P0_REG0, 0);

    // 复位
    I2CRegWrite(client, AIC31_P0_REG1, AIC31_RESET);
}

/**
 *  功能 初始化 AIC3106 数据模式和 slot 位宽
 *
 *  参数 slaveAddr : AIC3106 Slave address
 *         dataType : 设置数据类型模式
 *         slotWidth: 设置 Slot 的位宽
 *         dataOff  : 从字时钟的上升沿到捕获有效数据的时钟个数
 *
 *             dataType 的值可以设置为：
 *                 AIC31_DATATYPE_I2S - I2S 模式 \n
 *                 AIC31_DATATYPE_DSP - DSP 模式 \n
 *                 AIC31_DATATYPE_RIGHTJ - 右对齐模式 \n
 *                 AIC31_DATATYPE_LEFTJ -  左对齐模式 \n
 *
 *  返回 无
 *
 **/
static void AIC31DataConfig(struct i2c_client *client, unsigned char dataType, 
                     unsigned char slotWidth, unsigned char dataOff)
{
    unsigned char slot;

    switch(slotWidth)
    {
        case 16:
            slot = AIC31_SLOT_WIDTH_16;
        break;

        case 20:
            slot = AIC31_SLOT_WIDTH_20;
        break;

        case 24:
            slot = AIC31_SLOT_WIDTH_24;
        break;

        case 32:
            slot = AIC31_SLOT_WIDTH_32;
        break;

        default:
            slot = AIC31_SLOT_WIDTH_16;
        break;
    }

    // 设置数据模式和 slot 位宽
    I2CRegWrite(client, AIC31_P0_REG9, (dataType | slot));
  
    // 字时钟的上升沿到捕获有效数据的时钟个数
    I2CRegWrite(client, AIC31_P0_REG10, dataOff);

}

/**
 *  功能 设置 AIC3106 的采样率
 *
 *  参数 slaveAddr  : AIC3106 Slave address
 *         mode      : 选择 AIC3106 内部的 DAC 或者 ADC
 *         sampleRate: 采样率
 *
 *             mode 的值可以设置为：
 *                 AIC31_MODE_ADC -  选择 ADC \n
 *                 AIC31_MODE_DAC -  选择 DAC \n
 *                 AIC31_MODE_BOTH -  选择 ADC 和 DAC \n
 *             sampleRate 的值可以设置为：
 *                 FS_8000_HZ ,FS_11025_HZ ,FS_16000_HZ ,FS_22050_HZ ,FS_24000_HZ
 *                 FS_32000_HZ ,S_44100_HZ ,FS_48000_HZ ,FS_96000_HZ
 *             采样率 fs 由以下等式推导：
 *                 fs = (PLL_IN * [pllJval.pllDval] * pllRval) /(2048 * pllPval).
 *             其中 PLL_IN 由外部晶振输入   PLL_IN = 24576 kHz
 *
 *  返回 无
 *
 **/
static void AIC31SampleRateConfig(struct i2c_client *client, unsigned int mode, 
                           unsigned int sampleRate)
{
    unsigned char fs;
    unsigned char ref = 0x0Au;
    unsigned char temp;
    unsigned char pllPval = 4u;
    unsigned char pllRval = 1u;
    unsigned char pllJval = 16u; 
    unsigned short pllDval = 0u;

    // 采样率参数选择
    switch(sampleRate)
    {
        case 8000:
            fs = 0xAAu;
        break;

        case 11025:
            fs = 0x66u;
            ref = 0x8Au;
            pllJval = 14u;
            pllDval = 7000u;
        break;

        case 16000:
            fs = 0x44u;
        break;

        case 22050:
            fs = 0x22u;
            ref = 0x8Au;
            pllJval = 14u;
            pllDval = 7000u;
        break;

        case 24000:
            fs = 0x22u;
        break;
    
        case 32000:
            fs = 0x11u;
        break;

        case 44100:
            ref = 0x8Au;
            fs = 0x00u;
            pllJval = 14u;
            pllDval = 7000u;
        break;

        case 48000:
            fs = 0x00u;
        break;

        case 96000:
            ref = 0x6Au;
            fs = 0x00u;
        break;

        default:
            fs = 0x00u;
        break;
    }
    
    temp = (mode & fs);
   
    // 设置采样率
    I2CRegWrite(client, AIC31_P0_REG2, temp);
  
    I2CRegWrite(client, AIC31_P0_REG3, 0x80 | pllPval);

    // 使用 PLLCLK_IN 作为 MCLK
    I2CRegWrite(client, AIC31_P0_REG102, 0x08);

    // 使用 PLLDIV_OUT 作为 CODEC_CLKIN
    I2CRegBitClr(client, AIC31_P0_REG101, 0x01);

    temp = (pllJval << 2);
    I2CRegWrite(client, AIC31_P0_REG4, temp);

    // 初始化 PLL 分频寄存器
    I2CRegWrite(client, AIC31_P0_REG5, (pllDval >> 6) & 0xFF);
    I2CRegWrite(client, AIC31_P0_REG6, (pllDval & 0x3F) << 2);

    temp = pllRval;
    I2CRegWrite(client, AIC31_P0_REG11, temp);

    I2CRegWrite(client, AIC31_P0_REG7, ref);


}

/**
 *  功能 初始化 AIC3106 的 ADC 及其输出增益
 *
 *  参数 baseAddr  : AIC3106 Slave Address
 *         adcGain   : ADC 的输出增益
 *         inSource  : 模拟信号输入源
 *
 *             adcGain 的值可以设置为： 0~59.5，其值为0.5的倍数
 *             inSource的值可以设置为：
 *                 AIC31_LINE_IN -  选择 LINE IN \n
 *                 AIC31_MIC_IN -  选择 MIC IN \n
 *
 *  返回 无
 *
 **/  
static void AIC31ADCInit(struct i2c_client *client,  int adcGain, unsigned char inSource)
{ 
    //unsigned char adc_gain = adcGain/0.5;

   // 设置左右声道的 ADC 增益
    I2CRegWrite(client, AIC31_P0_REG15, 42);      //sin  mic录音的音量
    I2CRegWrite(client, AIC31_P0_REG16, 42);      //rin  usb 100过来的音量


    // 采样率参数选择
   switch(inSource)
   {
      case AIC31_LINE_IN:
                    // GPIO1 选择输出分频后的PLL IN
                   I2CRegWrite(client, AIC31_P0_REG98, 0x20);

                   // 使能编解码器作为主机用于输出fs 和 bclk
                   I2CRegWrite(client, AIC31_P0_REG8,  0x00);
                   I2CRegWrite(client, AIC31_P0_REG12, 0x50);
                   //I2CRegWrite(client, AIC31_P0_REG12, 0x00);

                   // Line L1L 上电
                   I2CRegWrite(client, AIC31_P0_REG19, 0x04);

                   // Line L1R 上电  ADCR
                   I2CRegWrite(client, AIC31_P0_REG22, 0x04);

                   // 连接 MIC IN 连接到 ADC  MIC3L to ADCL
                   I2CRegWrite(client, AIC31_P0_REG17, 0xFF);
                   I2CRegWrite(client, AIC31_P0_REG18, 0xFF);

                   // MIC IN 上电
                   I2CRegWrite(client, AIC31_P0_REG25, 0x80);
       break;

       //mic3L+line1R
//    case AIC31_LINE_IN:
//                      // GPIO1 选择输出分频后的PLL IN
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG98, 0x20);
//                     // 使能编解码器作为主机用于输出fs 和 bclk
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG8, 0xD0);
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG12, 0xA0);
//                     // Line L1L 上电
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG19, 0x7c);
//
//                     // Line L1R 上电  ADCR
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG22, 0x04);
//
//                     // 连接 MIC IN 连接到 ADC  MIC3L to ADCL
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG17, 0x0F);
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG18, 0xFF);
//
//                     // MIC IN 上电 2.5V
//                     I2CRegWrite(slaveAddr, AIC31_P0_REG25, 0x80);



       case AIC31_MIC_IN:
           // GPIO1 选择输出分频后的PLL IN
			I2CRegWrite(client, AIC31_P0_REG98, 0xA0);

			// 使能编解码器作为主机用于输出fs 和 bclk
			I2CRegWrite(client, AIC31_P0_REG8,  0xD1);
			
			// ADC上电
			I2CRegWrite(client, AIC31_P0_REG19, 0x74);
			I2CRegWrite(client, AIC31_P0_REG22, 0x74);

			//抽选滤波配置（降采样）
			I2CRegWrite(client, AIC31_P0_REG107, 0xE0);
			//数字MIC 数据输入通道（GPIO2）
		    I2CRegWrite(client, AIC31_P0_REG99, 0x70);
			// MIC IN 上电
			I2CRegWrite(client, AIC31_P0_REG25, 0x20);
		break;
	}
}


static void AIC31DACInit(struct i2c_client *client, int dacAtten)
{
#if 0
    unsigned char dac_atten = dacAtten/0.5;
#else
    unsigned char dac_atten = dacAtten*2;
#endif

	// 左右声道 DACs 上电
    I2CRegWrite(client, AIC31_P0_REG37, 0xE0);
    //I2CRegWrite(baseAddr, AIC31_P0_REG7, 0x1E);
    // 选择 DAC L1 R1 路径
    I2CRegWrite(client, AIC31_P0_REG41, 0x02);
    I2CRegWrite(client, AIC31_P0_REG42, 0x6C);

  //  I2CRegWrite(slaveAddr, AIC31_P0_REG40, 0x28);      //修改处
    //I2CRegWrite(slaveAddr, AIC31_P0_REG81, 0x6E);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG84, 0x6E);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG88, 0x6E);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG91, 0x6E);

    //I2CRegWrite(slaveAddr, AIC31_P0_REG80, 0x6E);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG83, 0x6E);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG87, 0x6E);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG90, 0x6E);
    // DAC L 连接到 HPLOUT
    //I2CRegWrite(slaveAddr, AIC31_P0_REG47, 0x80);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG51, 0x09);

    // DAC R 连接到 HPROUT
    //I2CRegWrite(slaveAddr, AIC31_P0_REG64, 0x80);
    //I2CRegWrite(slaveAddr, AIC31_P0_REG65, 0x09);


    I2CRegWrite(client, AIC31_P0_REG82, 0x80);
    I2CRegWrite(client, AIC31_P0_REG86, 0x09);


    I2CRegWrite(client, AIC31_P0_REG92, 0x80);
    I2CRegWrite(client, AIC31_P0_REG93, 0x09);

    I2CRegWrite(client, AIC31_P0_REG43, dac_atten);
    I2CRegWrite(client, AIC31_P0_REG44, dac_atten);
}

/****************************************************************************/
/*                                                                          */
/*              初始化 AIC31 音频芯片                                                                                                           */
/*                                                                          */
/****************************************************************************/
static void InitAIC31I2S(struct i2c_client *client)
{

    AIC31Reset(client);
    mdelay(50);


    AIC31DataConfig(client, AIC31_DATATYPE_I2S, SLOT_SIZE, AIC31_DATATYPE_I2S);


    AIC31SampleRateConfig(client, AIC31_MODE_BOTH, FS_48000_HZ);


    if(client->addr == I2C_SLAVE_CODEC_AIC31)
    {
    	AIC31ADCInit(client, ADC_GAIN_0DB, AIC31_LINE_IN);
    }
    else if(client->addr == I2C_SLAVE_CODEC_AIC31_1)
    {
#if 0    
    	AIC31ADCInitMaster(slaveAddr, ADC_GAIN_0DB, AIC31_LINE_IN);
#else
   //default no headset insert
   AIC31ADCInit(client, ADC_GAIN_0DB, AIC31_LINE_IN);
#endif
    }
   AIC31DACInit(client, DAC_ATTEN_0DB);
 
}

static void  aic3106_dts_parse(struct device*dev, struct aic3106_data *aic3106)
{
   aic3106->reset_pin = of_get_named_gpio(dev->of_node, "reset-gpio", 0);

   //dvdd(iovdd) 1.8v   => pmic rk808 vldo3 
   aic3106->dvdd = regulator_get(dev, "dvdd"); 
   if (IS_ERR(aic3106->dvdd)) {
       printk("regulator get of dvdd failed");
       aic3106->dvdd = NULL;
   }else{
          regulator_set_voltage(aic3106->dvdd,1800000,1800000);
   }
   
   //drvdd 3.0v  => pmic rk808 vldo4 
   aic3106->drvdd = regulator_get(dev, "drvdd"); 
   if (IS_ERR(aic3106->drvdd)) {
       printk("regulator get of dvdd failed");
       aic3106->drvdd = NULL;
   }else{
          regulator_set_voltage(aic3106->drvdd,3000000,3000000);
   }
}

static int aic3106_mic_mute(struct snd_soc_dai *dai, int mute)
{
   //struct aic3106_data *aic3106 = snd_soc_codec_get_drvdata(dai->codec);

   pr_err("aic3106 mic mute=%d !\n",mute);

   return 0;
}

static const struct snd_soc_dai_ops aic3106_dai_ops = {
   .digital_mute = aic3106_mic_mute,
};

static struct snd_soc_dai_driver aic3106_i2s_dai = {
   .name = "aic3106_aif1",
   .capture = {
       .stream_name = "Capture",
       .channels_min = 2,
       .channels_max = 8,
       .rates = (SNDRV_PCM_RATE_32000 |
             SNDRV_PCM_RATE_44100 |
             SNDRV_PCM_RATE_48000),
       .formats = (SNDRV_PCM_FMTBIT_S16_LE),
   },
   .ops = &aic3106_dai_ops,
};

static int aic3106_probe(struct snd_soc_codec *codec)
{
   struct aic3106_data *aic3106 = snd_soc_codec_get_drvdata(codec);

   pr_err("aic3106_probe !\n");
   aic3106->codec = codec;
   
   return 0;
}

static int aic3106_remove(struct snd_soc_codec *codec)
{
    pr_err("aic3106_remove !\n");
   
   return 0;
}

static int aic3106_suspend(struct snd_soc_codec *codec)
{
   dev_err(codec->dev, "aic3106_suspend !\n");
   
   return 0;
}

static int aic3106_resume(struct snd_soc_codec *codec)
{
   dev_err(codec->dev, "aic3106_resume !\n");

   return 0;
}

static unsigned int aic3106_read_reg_byte(struct snd_soc_codec *codec, unsigned int reg)
{
   int ret = -1;
   char buf[2];
   struct i2c_client *client=to_i2c_client(codec->dev);
       
    buf[0]=reg;
    ret=i2c_master_send(client, buf, 1);
   if(ret<0)
       pr_err("aic3106_read_reg: master send fail !\n");
   
    ret=i2c_master_recv(client, buf, 1);
    if(ret<0){
       pr_err("aic3106_read_reg: master recv fail !\n");
       return ret;
    }

    return buf[0];
}

static int aic3106_write_reg_byte(struct snd_soc_codec *codec, unsigned int reg,
       unsigned int value)
{
   char buf[2];
   int ret = -1;
   struct i2c_client *client=to_i2c_client(codec->dev);
   
    buf[0]=reg;
    buf[1]=value;
    ret = i2c_master_send(client, buf, 2);
    if(ret<0)
       pr_err("aic3106_write_reg_byte: master send fail !\n");

     return ret;
}
 
static const struct snd_soc_codec_driver aic3106_codec = {
   .probe =        aic3106_probe,
   .remove =       aic3106_remove,
   .suspend =  aic3106_suspend,
   .resume =       aic3106_resume,
#if 0
   //.set_bias_level = aic3106_set_bias_level,
   .controls =     aic3106_snd_controls,
   .num_controls =     ARRAY_SIZE(aic3106_snd_controls),
   .dapm_widgets =     aic3106_dapm_widgets,
   .num_dapm_widgets = ARRAY_SIZE(aic3106_dapm_widgets),
   .dapm_routes =      aic3106_dapm_routes,
   .num_dapm_routes =  ARRAY_SIZE(aic3106_dapm_routes),
#endif

   .read = aic3106_read_reg_byte,
   .write = aic3106_write_reg_byte,
#if 0
   .reg_cache_size = ARRAY_SIZE(aic3106_regs),
   .reg_word_size = sizeof(u8),
   .reg_cache_default = aic3106_regs,
#endif
};

static int aic3106_i2c_probe(struct i2c_client *i2c,
               const struct i2c_device_id *id)
{
   struct aic3106_data *aic3106;
   int ret;
   
   dev_err(&i2c->dev, "aic3106_i2c_probe: addr=0x%x\n" ,i2c->addr);

         if(i2c->addr == I2C_SLAVE_CODEC_AIC31_1){
       aic3106 = devm_kzalloc(&i2c->dev, sizeof(struct aic3106_data), GFP_KERNEL);
       if (!aic3106)
           return -ENOMEM;

       aic3106_dts_parse(&i2c->dev, aic3106);
       i2c_set_clientdata(i2c, aic3106);

       /*if(aic3106->dvdd)
           ret = regulator_enable(aic3106->dvdd);
       
       if(aic3106->drvdd)
           ret = regulator_enable(aic3106->drvdd);*/

       //hardware reset       gpio0 B3_D  U30  low->high
       gpio_set_value(aic3106->reset_pin, 0);
       mdelay(50);
       gpio_set_value(aic3106->reset_pin, 1);  
       mdelay(10);
       
       //register codec
       ret = snd_soc_register_codec(&i2c->dev, &aic3106_codec, &aic3106_i2s_dai, 1);
       if (ret != 0)
           dev_err(&i2c->dev, "Failed to register codec (%d)\n", ret);
       
         }

   //init aic3106 register
   InitAIC31I2S(i2c);  
   dev_err(&i2c->dev, "aic3106_i2c_probe: success!\n");
   ret =0;//temp add for test
   return ret;
}

static int aic3106_i2c_remove(struct i2c_client *client)
{
   snd_soc_unregister_codec(&client->dev);
   return 0;
}

static const struct i2c_device_id aic3106_i2c_id[] = {
   { "aic3106", 0 },
   { }
};

#ifdef CONFIG_OF
static struct of_device_id aic3106_match_table[] = {
   {
       .compatible = "ti,aic3106",
   },
   {},
};
MODULE_DEVICE_TABLE(of, aic3106_match_table);
#endif

static struct i2c_driver aic3106_i2c_driver = {
   .driver = {
       .name = "aic3106",
       .owner = THIS_MODULE,
       .of_match_table = aic3106_match_table,
   },
   .probe =    aic3106_i2c_probe,
   .remove =   aic3106_i2c_remove,
   .id_table = aic3106_i2c_id,
};

module_i2c_driver(aic3106_i2c_driver);

MODULE_DESCRIPTION("Ti tlv320aic3106 Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("codec:tlv320aic3106-i2s");
