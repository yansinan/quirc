// Include MicroPython API.
#include "py/obj.h" //MP_ROM_QSTR, MP_ROM_QSTR
#include "py/runtime.h"
#include "py/objlist.h" //mp_obj_new_list
#include "py/objstr.h" // GET_STR_DATA_LEN
#include "py/builtin.h" // Many a time you will also need py/builtin.h, where the python built-in functions and modules are declared
#include <string.h> //for memcpy

#define LIST_MIN_ALLOC_DR 4
void *m_malloc_dr(size_t num_bytes) {
    void *ptr = malloc(num_bytes);
    if (ptr == NULL && num_bytes != 0) {
        m_malloc_fail(num_bytes);
    }
    #if MICROPY_MEM_STATS
    MP_STATE_MEM(total_bytes_allocated) += num_bytes;
    MP_STATE_MEM(current_bytes_allocated) += num_bytes;
    UPDATE_PEAK();
    #endif
    // printf("m_malloc_dr %d : %p\n", num_bytes, ptr);
    return ptr;
}
#define m_new_dr(type, num) ((type *)(m_malloc_dr(sizeof(type) * (num))))
#define m_new_obj_dr(type) (m_new_dr(type, 1))
#define mp_seq_clear_dr(start, len, alloc_len, item_sz) memset((byte *)(start) + (len) * (item_sz), 0, ((alloc_len) - (len)) * (item_sz))

// freeRTOS
#include "freertos/FreeRTOS.h" //configMAX_PRIORITIES
#include "freertos/task.h"
#include "freertos/portmacro.h" //TickType_t

STATIC void loop_xtask_buf();
// quirc
#include "../lib/decode.c"
#include "../lib/identify.c"
#include "../lib/version_db.c"
#include "../lib/quirc.c"
// quirc--end

///////////////////////////////////定义quirc type
// c用到的数据，用来处理对应的uPy数据；传入buf所需的结构体
/**
 * @brief Data structure of frame buffer 部分同esp_camera.h
 */
typedef struct {
    uint8_t * buf;              /*!< Pointer to the pixel data */
    size_t len;                 /*!< Length of the buffer in bytes */
    size_t width;               /*!< Width of the buffer in pixels */
    size_t height;              /*!< Height of the buffer in pixels */
    // pixformat_t format;         /*!< Format of the pixel data */
    // struct timeval timestamp;   /*!< Timestamp since boot of the first DMA buffer of the frame */
} quirc_fb_t;
// 自定义结构体
typedef struct _mp_obj_quirc_t
{
    mp_obj_base_t base;
    struct quirc *qr;
    quirc_fb_t fb;
    mp_obj_t mp_cb_decode; //decode完成的回调函数
    TaskHandle_t handle_xtask_decode;

    bool is_debug;
} mp_obj_quirc_t;

// 全局指针，等价于 mp_obj_quirc_t *self
mp_obj_quirc_t obj_quirc={
    .fb={
        .buf=NULL,
        .len=0,
        .width=0,
        .height=0
    },
    .qr=NULL,
    .mp_cb_decode=NULL,
    .handle_xtask_decode=NULL,
    .is_debug=false
};
const mp_obj_type_t mp_obj_quirc_type;
STATIC mp_obj_t close_quirc();


/*
#ifdef CONFIG_ESP32CAM
    #include "esp_log.h"
	#include "esp_camera.h"

    #define TAG "camera"
    //WROVER-KIT PIN Map
    #define CAM_PIN_PWDN    32 //power down is not used
    #define CAM_PIN_RESET   -1 //software reset will be performed
    #define CAM_PIN_XCLK     0
    #define CAM_PIN_SIOD    26 // SDA
    #define CAM_PIN_SIOC    27 // SCL

    #define CAM_PIN_D7      35
    #define CAM_PIN_D6      34
    #define CAM_PIN_D5      39
    #define CAM_PIN_D4      36
    #define CAM_PIN_D3      21
    #define CAM_PIN_D2      19
    #define CAM_PIN_D1      18
    #define CAM_PIN_D0       5
    #define CAM_PIN_VSYNC   25
    #define CAM_PIN_HREF    23
    #define CAM_PIN_PCLK    22
    static camera_config_t camera_config = 
    {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_GRAYSCALE,//YUV422,GRAYSCALE,RGB565,JPEG
        .frame_size = FRAMESIZE_QVGA,//QQVGA-UXGA Do not use sizes above QVGA when not JPEG

        .jpeg_quality = 10, //0-63 lower number means higher quality
        .fb_count = 1 //if more than one, i2s runs in continuous mode. Use only with JPEG
    };
    STATIC mp_obj_t camera_init()
    {
        esp_err_t err = esp_camera_init(&camera_config);
        if (err != ESP_OK) 
        {
            // ESP_LOGE(TAG, "Camera in qrcode init failed");
            printf("Camera init failed\n");
            return mp_const_false;
        }
        printf("Camera in qrcode init success!!\n");

        printf("quirc camera_init obj_quirc.qr=> !qr %d \n",(!obj_quirc.qr));
        if (!obj_quirc.qr) {
	       obj_quirc.qr = quirc_new();
        	if (!obj_quirc.qr) {
               printf("couldn't allocate QR decoder\n");
               goto fail_qr;
           }
        }
        if (quirc_resize(obj_quirc.qr, 640, 480) < 0) {
            printf("couldn't allocate QR buffer\n");
            goto fail_qr_resize;
        }
        printf("quirc_resize() done\n");
        return mp_const_true;
    fail_qr_resize:
    fail_qr:
	    return mp_obj_new_int(0);
    }
    STATIC MP_DEFINE_CONST_FUN_OBJ_0(dr_mp_camera_init_obj, camera_init);

#endif
*/
// #define DR_DEBUG_PRINT
// debug输出
// 测试用，查看buf内容
STATIC void printBuf(const uint8_t *in_buf,int in_len,char *in_msg ){
#ifdef DR_DEBUG_PRINT
    for (int ii = 0; ii < 10; ii++){
        printf("x%x,", *(in_buf + ii));
	}
    printf("%s buf[0:10]: %d len %d\n",in_msg, (int)in_buf,in_len);
#endif
}
// 解决mp_obj_new_str的bug，把功能层层展开
STATIC mp_obj_t mp_obj_new_str_dr(const char *data, size_t len)
{
    // printf("begin mp_obj_new_str_dr \n");
    // printf("begin mp_obj_new_str_dr %p %d \n",data,len);
    size_t len_mp_obj_str_t=sizeof(mp_obj_str_t) * (1);
    // printf("mp_obj_new_str_dr %d \n",len_mp_obj_str_t);
    mp_obj_str_t *o = (mp_obj_str_t *)(malloc(len_mp_obj_str_t));
    if (o == NULL && len_mp_obj_str_t != 0) {
        printf("!!!!!!!!!!!!!mp_obj_new_str_dr.malloc fail \n");
        m_malloc_fail(len_mp_obj_str_t);
    }
    o->base.type = &mp_type_str;
    o->len = len;
    if(data){
        // o->hash = qstr_compute_hash((const byte *)"_const_str_out1", len);
        // byte *p = m_new(byte, 15 + 1);
        int len_m_byte=sizeof(byte) * (len+1);
        byte *p = (byte *)(malloc(len_m_byte));
        
        o->data = p;
        // printf("after byte o->data \n");
        memcpy(p, (const byte *)data, len * sizeof(byte));
        p[len] = '\0'; // for now we add null for compatibility with C ASCIIZ strings
    }
    // printf("all done mp_obj_test before MP_OBJ_FROM_PTR \n");
    return MP_OBJ_FROM_PTR(o);
}

// 解决mp_obj_list问题
STATIC mp_obj_t mp_obj_new_list_dr(size_t n, mp_obj_t *items) {
    mp_obj_list_t *o = m_new_obj_dr(mp_obj_list_t);
    o->base.type = &mp_type_list;
    o->alloc = n < LIST_MIN_ALLOC_DR ? LIST_MIN_ALLOC_DR : n;
    o->len = n;
    o->items = m_new_dr(mp_obj_t, o->alloc);
    mp_seq_clear_dr(o->items, n, o->alloc, sizeof(*o->items));
    if (items != NULL) {
        for (size_t i = 0; i < n; i++) {
            o->items[i] = items[i];
        }
    }
    return MP_OBJ_FROM_PTR(o);
}

// 初始化quirc,xTask
STATIC bool _init_quirc(const int in_local_w,const int in_local_h){
    bool is_quirc_new=false;
    if (!obj_quirc.qr) {
        obj_quirc.qr = quirc_new();
        is_quirc_new=true;
        if (!obj_quirc.qr) {
            close_quirc();
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("couldn't allocate QR buffer"));
            return false;
        }
    }
    //设置两个初始值，保证至少一次ressize
    if(in_local_w==0 && obj_quirc.fb.width==0)obj_quirc.fb.width=320;
    if(in_local_h==0 && obj_quirc.fb.height==0)obj_quirc.fb.height=240;
    const int new_w=in_local_w==0?obj_quirc.fb.width:in_local_w;
    const int new_h=in_local_h==0?obj_quirc.fb.height:in_local_h;
    // printf("before resize: new wxh:%d,%d|in_local_wxh：%d,%d|,obj_quirc.fb.wxh:%d,%d \n",new_w,new_h,in_local_w,in_local_h,obj_quirc.fb.width,obj_quirc.fb.height);
    if(is_quirc_new || obj_quirc.fb.width!=new_w || obj_quirc.fb.height!=new_h){
        if (quirc_resize(obj_quirc.qr, new_w, new_h) < 0) {
            close_quirc();
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("couldn't resize QR buffer"));
            return false;
        }
        obj_quirc.fb.width=new_w;
        obj_quirc.fb.height=new_h;
        printf("quirc_resize() done %d x %d\n",in_local_w,in_local_h);
    }

    // 给xTask
    if(!obj_quirc.handle_xtask_decode){
        // 测试过:20*1024不够
        int mem_need=(obj_quirc.fb.width*obj_quirc.fb.height > 320*240) ? (80*1024)+8896 : 30*1024;//8896是quirc.payload的数组长度
        if (xTaskCreatePinnedToCore(
            loop_xtask_buf, 
            "task_quirc_read",
            mem_need,
            NULL, 
            configMAX_PRIORITIES - 3, 
            &obj_quirc.handle_xtask_decode,
            1) != pdPASS) {
            close_quirc();
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to create quirc task"));
            return false;
        }
        !obj_quirc.is_debug?0:printf("xTask begin %p \n",obj_quirc.handle_xtask_decode);
    }
    return true;
}

// c函数，处理uint8_t *buf
STATIC int _feed_buf()
{
    // printf("quirc _feed_buf obj_quirc.qr=> !obj_quirc.qr %d \n",(!obj_quirc.qr));
    if(!_init_quirc(obj_quirc.fb.width,obj_quirc.fb.height))goto fail_qr;
    
    /* Fill out the image buffer here. image is a pointer to a w*h bytes. One byte per pixel, w pixels per line, h lines in the buffer.*/
    uint8_t *image = quirc_begin(obj_quirc.qr, NULL, NULL);
    memcpy(image, obj_quirc.fb.buf, obj_quirc.fb.len);
    // printBuf(image,obj_quirc.fb.len,"before quirc_end image");
    quirc_end(obj_quirc.qr);
    // 打印检查
    // printBuf(image,obj_quirc.fb.len,"after quirc_end image");
    int num_codes = quirc_count(obj_quirc.qr);
    printf("read cnt_codes in buf----------------------> %d\n", num_codes);

    return num_codes;
// fail_qr_resize:
fail_qr:
	return -1;
}

// 解码
STATIC int _decode_qrcode(char **list_str_res,int num_codes){
    if (!obj_quirc.qr) {
        printf("couldn't allocate QR decoder\n");
        close_quirc();
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("allocate QR decoder NULL"));
        return false;
    }
    // int num_codes= sizeof(*list_str_res) / sizeof(**list_str_res);
    if(num_codes>0){
        int len_all=0;

        /* We've previously fed an image to the decoder via quirc_begin/quirc_end.*/
        for (int i = 0; i < num_codes; i++) {
            struct quirc_code code;            
            quirc_extract(obj_quirc.qr, i, &code);

            /* Decoding stage */
            struct quirc_data data;
            quirc_decode_error_t err = quirc_decode(&code, &data);
            // mp_obj_t mp_obj_str_msg;
            if (err){
                len_all+=strlen(quirc_strerror(err));

                list_str_res[i]=heap_caps_malloc(strlen(quirc_strerror(err)), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                strcpy(list_str_res[i],(char *)quirc_strerror(err));
                !obj_quirc.is_debug?0:printf("Quirc DECODE:FAILED list_str_res: %s\n", list_str_res[i]);

            }else{
                len_all+=data.payload_len;

                list_str_res[i]=heap_caps_malloc(data.payload_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                strcpy(list_str_res[i],(char *)data.payload);
                !obj_quirc.is_debug?0:printf("Quirc DECODE:OK Data list_str_res: %s\n", list_str_res[i]);
            }
        }
        return len_all;
    }
    return 0;
}

// 触发micropython的回调函数
STATIC bool _cb_fun_decode_res(mp_obj_t mp_cb_decode,char ** list_str_res,int cnt_codes,int len_str_res_join){
    // micropython callback
    if (mp_cb_decode != mp_const_none && cnt_codes>0 && mp_obj_is_callable(mp_cb_decode) && len_str_res_join>0){
        // mp_call_function_1_protected(mp_cb_decode, mp_obj_new_str(str_res_long,strlen(str_res_long)));
        mp_obj_t list_mp_str_res[cnt_codes];
        for(int i=0;i<cnt_codes;i++){
            list_mp_str_res[i]=mp_obj_new_str_dr((const char *)list_str_res[i],strlen(list_str_res[i]));
            free(list_str_res[i]);
        }
        mp_sched_schedule(mp_cb_decode, mp_obj_new_list_dr(cnt_codes, list_mp_str_res));
        return true;
    }
    return false;
}

// xTask用的循环检索buf，无buf挂起
STATIC void loop_xtask_buf(){

    // 阻塞500ms.
    // const TickType_t xDelay = 500 / portTICK_PERIOD_MS;
    while (1)
    {
        if(obj_quirc.fb.buf){
            // feed buf
            !obj_quirc.is_debug?0:printf("xTask:before _feed_buf obj_quirc.fb.buf != NULL \n");
            int cnt_codes=_feed_buf();
            !obj_quirc.is_debug?0:printf("xTask:after _feed_buf=%d \n",cnt_codes);
            if(cnt_codes>0){
                // decode
                char *list_str_res[cnt_codes];
                int len_str_res_join= _decode_qrcode(list_str_res,cnt_codes);//返回所有字符串连接后的总长度
                // callback
                _cb_fun_decode_res(obj_quirc.mp_cb_decode,list_str_res,cnt_codes,len_str_res_join);
            }
            obj_quirc.fb.buf=NULL;
            !obj_quirc.is_debug?0:printf("xTask:after buf=NULL \n");
        }
        vTaskSuspend(NULL);
        // vTaskDelay( xDelay );
    }
    // 清除xTask
    close_quirc();
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("quirc.xTask loop out,delete"));
}
// -------------c 处理结束

// -------------module 直接使用的接口
// micropython接口，设置宽高，//？必须先设定？
STATIC mp_obj_t mp_init_quirc(mp_obj_t in_int_w,mp_obj_t in_int_h)
{
    if(!_init_quirc(mp_obj_get_int(in_int_w),mp_obj_get_int(in_int_h)))return mp_const_false;
    else return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(dr_mp_init_obj, mp_init_quirc);
 
//micropython接口，传入bytes,图像，cbFuntion;宽，高 之前设定，或type quirc设定，或默认值
STATIC mp_obj_t feed_buf(const mp_obj_t in_mp_obj_fb,mp_obj_t mp_cb_decode)
{   
    if(!obj_quirc.fb.buf){
//#ifdef CONFIG_ESP32CAM
//    camera_fb_t *fb = NULL;
//    fb = esp_camera_fb_get();
//    if (!fb) {
//        printf("Camera in qrcode Camera capture failed\n");
//        // ESP_LOGE(TAG, "Camera capture failed");
//    }
//    obj_quirc.fb.buf= fb->buf;
//    obj_quirc.fb.width = fb->width;
//    obj_quirc.fb.height = fb->height;
//    obj_quirc.fb.len=fb->len;
//#else
    // 从micorypython传回b""类型的图片数据，转成uint8_t *
        GET_STR_DATA_LEN(in_mp_obj_fb, byte_buf, len_byte_buf);
        // 赋予静态变量
        !obj_quirc.is_debug?0:printBuf(byte_buf,len_byte_buf,"byte_buf receive=>");
        obj_quirc.fb.buf=(uint8_t *)byte_buf;
        obj_quirc.fb.len=(size_t)len_byte_buf;
        !obj_quirc.is_debug?0:printBuf(obj_quirc.fb.buf,obj_quirc.fb.len,"obj_quirc.buf got=>");
        if(obj_quirc.handle_xtask_decode){
            vTaskResume(obj_quirc.handle_xtask_decode);
            !obj_quirc.is_debug?0:printf("vTaskResume %p \n",obj_quirc.handle_xtask_decode);
        }else{
            close_quirc();
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("quirc.xTask handle NULL"));
        }

        return mp_const_true;
//#endif
    }

//#ifdef CONFIG_ESP32CAM
//    esp_camera_fb_return(fb);
//#endif
    return mp_const_false;
}
// Define a Python reference to the function above.
STATIC MP_DEFINE_CONST_FUN_OBJ_2(dr_mp_feed_buf_obj, feed_buf);


// set cbFun of decode
STATIC mp_obj_t set_quirc_cb(mp_obj_t in_cb_fun){
    if(in_cb_fun != mp_const_none && mp_obj_is_callable(in_cb_fun))obj_quirc.mp_cb_decode=in_cb_fun; // 保存到全局cbFunction
    // else mp_cb_decode=NULL;
    return obj_quirc.mp_cb_decode;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dr_mp_quirc_cb_obj, set_quirc_cb);


// //micropython接口，传入num_codes
// STATIC mp_obj_t decode_qrcode(mp_obj_t in_cnt_codes,mp_obj_t in_cb_fun)
// {
//     int cnt_codes = mp_obj_get_int(in_cnt_codes);
//     mp_obj_t mp_cb_decode=set_quirc_cb(in_cb_fun);
    
//     char *list_str_res[cnt_codes];
//     int len_str_res_join= _decode_qrcode(list_str_res,cnt_codes);//返回所有字符串连接后的总长度

//     // callback
//     bool is_decode_suc=_cb_fun_decode_res(mp_cb_decode,list_str_res,cnt_codes,len_str_res_join);
//     return is_decode_suc?mp_const_true:mp_const_false;//mp_obj_new_tuple(num_codes, out_tuple);    
// }
// // Define a Python reference to the function above.
// STATIC MP_DEFINE_CONST_FUN_OBJ_2(dr_mp_quirc_decode_obj, decode_qrcode);

// 清理quirc函数
STATIC mp_obj_t close_quirc()
{
    // printf("quirc close=> !qr %d \n",(!obj_quirc.qr));
    if (obj_quirc.qr) {        
        quirc_destroy(obj_quirc.qr);
        obj_quirc.qr=NULL;
    }
    // 清除xTask
    if(obj_quirc.handle_xtask_decode){
        vTaskDelete(obj_quirc.handle_xtask_decode);
        obj_quirc.handle_xtask_decode=NULL;
    }
    // 公共变量
    obj_quirc.fb.buf=NULL;
    obj_quirc.fb.width=0;
    obj_quirc.fb.height=0;
    obj_quirc.fb.len=0;

    obj_quirc.mp_cb_decode=mp_const_none;
// #ifdef CONFIG_ESP32CAM
    // esp_err_t err = esp_camera_deinit();
	// if (err != ESP_OK) 
	// {
		// ESP_LOGE(TAG, "Camera deinit failed");
		// return mp_const_false;
	// }
	// return mp_const_true;
// #endif
    printf("quirc close:success \n");
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(dr_mp_close_quirc, close_quirc);

///////////////////////////////////定义quirc type

//quirc的成员函数 
// set cbFun of decode
STATIC mp_obj_t quirc_cb(mp_obj_t self_in,mp_obj_t in_cb_fun){
    // mp_obj_quirc_t *self = MP_OBJ_TO_PTR(self_in);  //从第一个参数中提取对象指针
    // if(!set_quirc_cb(in_cb_fun))self->mp_cb_decode=mp_cb_decode;
    // else self->mp_cb_decode = mp_const_none;
    return set_quirc_cb(in_cb_fun);
    // return self->mp_cb_decode;
} 
STATIC MP_DEFINE_CONST_FUN_OBJ_2(quirc_cb_obj,quirc_cb);
// feed buf for quirc
STATIC mp_obj_t quirc_feed(mp_obj_t self_in , const mp_obj_t in_mp_obj_fb){//,mp_obj_t in_mp_cb_decode 
    mp_obj_quirc_t *self = MP_OBJ_TO_PTR(self_in);  //从第一个参数中提取对象指针
    return feed_buf(in_mp_obj_fb,self->mp_cb_decode);
} 
STATIC MP_DEFINE_CONST_FUN_OBJ_2(quirc_feed_obj,quirc_feed);
// 关闭
STATIC mp_obj_t quirc_close(mp_obj_t self_in){
    // mp_obj_quirc_t *self = MP_OBJ_TO_PTR(self_in);  //从第一个参数中提取对象指针
    return close_quirc();
} 
STATIC MP_DEFINE_CONST_FUN_OBJ_1(quirc_close_obj,quirc_close);
////////////////////如果quirc有成员函数，定义完成

////////////////////begin----type的基本定义要求
// 定义type的locals_dict_type 
STATIC const mp_rom_map_elem_t quirc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_feed), MP_ROM_PTR(&quirc_feed_obj) },//喂图
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&quirc_close_obj) },//关闭清理
    { MP_ROM_QSTR(MP_QSTR_set_cb), MP_ROM_PTR(&quirc_cb_obj) },//设置回调函数
}; 
//定义字典的宏 
STATIC MP_DEFINE_CONST_DICT(quirc_locals_dict,quirc_locals_dict_table);
// 定义构造函数
STATIC mp_obj_t quirc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, 4, true);// 检查参数个数，最少1个参数，最多3个参数
    mp_obj_quirc_t *self = &obj_quirc;
    self->base.type = &mp_obj_quirc_type;
    //// 赋予静态变量//改在_init_quirc赋值
    self->fb.width = mp_obj_get_int(args[0]);//识别图像缩放宽
    self->fb.height = mp_obj_get_int(args[1]);//识别图像缩放高
    bool is_inited=_init_quirc(self->fb.width,self->fb.height);

    if(n_args==3)quirc_cb(self,args[2]); //可选第三个参数，cbFunction
    if(n_args==4)self->is_debug=mp_obj_is_true(args[3]); //可选第四个参数，是否debug输出

    !obj_quirc.is_debug?0:printf("quirc_make_new _init_quirc return %d \n",(int)is_inited);
    
    
    return MP_OBJ_FROM_PTR(self);
}
const mp_obj_type_t mp_obj_quirc_type = { 
    .base           =   { &mp_type_type }, 
    .name           =   MP_QSTR_quirc,           //名字要在这里定义，不是写在DICT中，同样要经过注册才行，但是这个单词已经被注册过了，所以就不用重复注册了
    .make_new       =   quirc_make_new,     //构造函数
    .locals_dict    =  (mp_obj_dict_t*)&quirc_locals_dict,  //注册quirc_locals_dict
};
///////////////////////////////////定义quirc type------------end

// Define all properties of the module.
// Table entries are key/value pairs of the attribute name (a string)
// and the MicroPython object reference.
// All identifiers and strings are written as MP_QSTR_xxx and will be
// optimized to word-sized integers by the build system (interned strings).
STATIC const mp_rom_map_elem_t dr_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_dr) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_quirc), (mp_obj_t)&mp_obj_quirc_type },
    
    { MP_ROM_QSTR(MP_QSTR___del__),  MP_ROM_PTR(&dr_mp_close_quirc) },

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&dr_mp_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_feed_quirc), MP_ROM_PTR(&dr_mp_feed_buf_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_quirc_callback), MP_ROM_PTR(&dr_mp_quirc_cb_obj) },
    // { MP_ROM_QSTR(MP_QSTR_decode_quirc), MP_ROM_PTR(&dr_mp_quirc_decode_obj) },
    { MP_ROM_QSTR(MP_QSTR_close_quirc), MP_ROM_PTR(&dr_mp_close_quirc) },
    // { MP_ROM_QSTR(MP_QSTR_init_camera), MP_ROM_PTR(&dr_mp_camera_init_obj) },
};
STATIC MP_DEFINE_CONST_DICT(dr_module_globals, dr_module_globals_table);

// Define module object.
const mp_obj_module_t dr_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dr_module_globals,
};

// Register the module to make it available in Python.
// Note: the "1" in the third argument means this module is always enabled.
// This "1" can be optionally replaced with a macro like MODULE_CEXAMPLE_ENABLED
// which can then be used to conditionally enable this module.
MP_REGISTER_MODULE(MP_QSTR_dr, dr_cmodule, 1);
