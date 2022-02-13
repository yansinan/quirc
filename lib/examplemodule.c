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
    printf("m_malloc_dr %d : %p\n", num_bytes, ptr);
    return ptr;
}
#define m_new_dr(type, num) ((type *)(m_malloc_dr(sizeof(type) * (num))))
#define m_new_obj_dr(type) (m_new_dr(type, 1))
#define mp_seq_clear_dr(start, len, alloc_len, item_sz) memset((byte *)(start) + (len) * (item_sz), 0, ((alloc_len) - (len)) * (item_sz))

// quirc
#include "decode.c"
#include "identify.c"
#include "version_db.c"
#include "quirc.c"
// quirc--end
// 自定义结构体
STATIC struct quirc *qr_global;
STATIC uint8_t *buf;
STATIC size_t len_buf;
STATIC int in_w=0,in_h=0;
STATIC mp_obj_t mp_cb_decode=mp_const_none;

// freeRTOS
#include "freertos/FreeRTOS.h" //configMAX_PRIORITIES
#include "freertos/task.h"
#include "freertos/portmacro.h" //TickType_t
TaskHandle_t handle_xtask_decode=NULL;
STATIC void loop_xtask_buf();

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

        printf("quirc camera_init qr_global=> !qr %d \n",(!qr_global));
        if (!qr_global) {
	       qr_global = quirc_new();
        	if (!qr_global) {
               printf("couldn't allocate QR decoder\n");
               goto fail_qr;
           }
        }
        if (quirc_resize(qr_global, 640, 480) < 0) {
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
// 测试用，查看buf内容
STATIC void printBuf(uint8_t *in_buf,int in_len,char *in_msg )
{
    for (int ii = 0; ii < 10; ii++){
        printf("x%x,", *(in_buf + ii));
	}
    printf("%s buf[0:10]: %d len %d\n",in_msg, (int)in_buf,in_len);
}

// 解决mp_obj_new_str的bug，把功能层层展开
STATIC mp_obj_t mp_obj_new_str_dr(const char *data, size_t len)
{
    printf("begin mp_obj_new_str_dr \n");
    printf("begin mp_obj_new_str_dr %p %d \n",data,len);
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
        printf("after byte o->data \n");
        memcpy(p, (const byte *)data, len * sizeof(byte));
        p[len] = '\0'; // for now we add null for compatibility with C ASCIIZ strings
    }
    printf("all done mp_obj_test before MP_OBJ_FROM_PTR \n");
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
STATIC bool _init_quirc(const int in_local_w,const int in_local_h){
    if (!qr_global) {
        qr_global = quirc_new();
        if (!qr_global) {
            printf("couldn't allocate QR decoder\n");
            return false;
        }
    }
    //设置两个初始值，保证至少一次ressize
    if(in_local_w==0 && in_w==0)in_w=320;
    if(in_local_h==0 && in_h==0)in_h=240;
    const int new_w=in_local_w==0?in_w:in_local_w;
    const int new_h=in_local_h==0?in_h:in_local_h;
    if(in_w!=in_local_w || in_h!=in_local_h){
        if (quirc_resize(qr_global, new_w, new_h) < 0) {
            printf("couldn't allocate QR buffer\n");
            return false;
        }
        in_w=new_w;
        in_h=new_h;
        printf("quirc_resize() done %d x %d\n",in_local_w,in_local_h);
    }

    // 给xTask
    if(!handle_xtask_decode){
        if (xTaskCreate(
            loop_xtask_buf, 
            "task_quirc_read",
            100*1024, 
            NULL, 
            configMAX_PRIORITIES - 3, 
            &handle_xtask_decode) != pdPASS) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to create quirc task"));
            return false;
        }
        printf("xTask begin %d \n",(int)handle_xtask_decode);
    }
    return true;
}
// c函数，处理uint8_t *buf
STATIC int _feed_buf()//uint8_t *buf, size_t len_buf,int in_w,int in_h
{
    // printf("quirc _feed_buf qr_global=> !qr_global %d \n",(!qr_global));
    if(!_init_quirc(in_w,in_h))goto fail_qr;
    
    /* Fill out the image buffer here. image is a pointer to a w*h bytes. One byte per pixel, w pixels per line, h lines in the buffer.*/
    uint8_t *image = quirc_begin(qr_global, NULL, NULL);
    memcpy(image, buf, len_buf);
    // printBuf(image,len_buf,"before quirc_end image");
    quirc_end(qr_global);
    // 打印检查
    // printBuf(image,len_buf,"after quirc_end image");
    int num_codes = quirc_count(qr_global);
    printf("readQRcode once finished num_codes----------------------> %d\n", num_codes);

    return num_codes;
// fail_qr_resize:
fail_qr:
	return -1;
}
STATIC void * _decode_qrcode(int num_codes,char **out_str_join,size_t *out_len){
    if (!qr_global) {
        printf("couldn't allocate QR decoder\n");
        buf=NULL;
        return false;
        // return mp_const_false;
    }
    if(num_codes>0){
        //给cbFun的输出对象
        mp_obj_t list_mp_obj[num_codes];
        //mp_obj_t out_tuple = mp_obj_new_tuple((size_t)num_codes, NULL);
        // for (int i = 0; i < num_codes; i++) {
        //    list_mp_obj[i] = mp_obj_new_str("1234567890",11);
        // }
        char *list_str_res[num_codes];
        int len_all=0;

        /* We've previously fed an image to the decoder via quirc_begin/quirc_end.*/
        for (int i = 0; i < num_codes; i++) {
            struct quirc_code code;
            
            quirc_extract(qr_global, i, &code);
            /* Decoding stage */
            struct quirc_data data;
            quirc_decode_error_t err = quirc_decode(&code, &data);
            // mp_obj_t mp_obj_str_msg;
            if (err){
                len_all+=strlen(quirc_strerror(err));

                printf("Quirc DECODE:FAILED: %s\n", quirc_strerror(err));
                // list_str_res[i]=(char *)quirc_strerror(err);
                list_str_res[i]=heap_caps_malloc(strlen(quirc_strerror(err)), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                strcpy(list_str_res[i],(char *)quirc_strerror(err));

                printf("Quirc DECODE:FAILED list_str_res: %s\n", list_str_res[i]);
                list_mp_obj[i]=mp_obj_new_str_dr(quirc_strerror(err),strlen(quirc_strerror(err)));
            }else{
                printf("Quirc DECODE:OK Data: %s\n", data.payload);
                len_all+=data.payload_len;

                list_str_res[i]=heap_caps_malloc(data.payload_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                strcpy(list_str_res[i],(char *)data.payload);
                printf("Data list_str_res: %s\n", list_str_res[i]);

                list_mp_obj[i]=mp_obj_new_str_dr((const char *)data.payload,data.payload_len);
            }
            // mp_obj_list_append(ret_list, mp_obj_str_msg);//可能是因为引用的问题，mp_obj一系列在xTask任务中，访问都出错

        }
        char str_res_long[len_all];
        printf("str_res_long.len_all: %d\n", len_all);
        //// memset(str_data,",",len_all);
        for (int i = 0; i < num_codes; i++) {
            strcat(str_res_long,list_str_res[i]);
            free(list_str_res[i]);
            printf("str_res_long: %s\n", str_res_long);
        }
        printf("str_res_long final: %s ,len=%d \n", str_res_long,len_all);
        *out_len=len_all;
        *out_str_join=(char *)heap_caps_malloc(len_all, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        strcpy(*out_str_join,str_res_long);
        printf("out_str_join final ptr: %d, string:%s ,len=%d \n",(int)*out_str_join, *out_str_join,*out_len);

        // micropython callback
        if (mp_cb_decode != mp_const_none && num_codes>0 && mp_obj_is_callable(mp_cb_decode)){
            // mp_sched_schedule(mp_cb_decode, mp_const_true);
            mp_sched_schedule(mp_cb_decode, mp_obj_new_list_dr(num_codes, list_mp_obj));//mp_obj_new_str(str_res_long,len_all)//mp_obj_new_tuple((size_t)num_codes,out_tuple)
        }
        return str_res_long;

    }
    return "fail";
}

char *str_decode_join=NULL;
size_t len_str_decode_join=0;

// xTask用的循环检索buf
STATIC void loop_xtask_buf(){

    // 阻塞500ms.
    // const TickType_t xDelay = 500 / portTICK_PERIOD_MS;
    /*
    qr_global = quirc_new();
    if (!qr_global) {
        printf("couldn't allocate QR decoder\n");
        goto fail_qr;
    }
    if (quirc_resize(qr_global, in_w, in_h) < 0) {
        printf("couldn't allocate QR buffer\n");
        goto fail_qr_resize;
    }
    printf("quirc_resize() done\n");
    */
    int cnt_codes=0;
    
    // printf("before qstr_find_strn before while \n");
    // // qstr q = qstr_find_strn("_const_str_out1",15);
    // // if (q != MP_QSTRnull) {
    // //     // qstr with this data already exists
    // //     printf("before while MP_OBJ_NEW_QSTR \n");
    // //     MP_OBJ_NEW_QSTR(q);
    // // } else {
    //     // no existing qstr, don't make one
    //     printf("before while mp_obj_new_str_copy \n");
    //     printf("before while mp_obj_new_str_copy %d \n",(int)&mp_type_str);
    //     // mp_obj_new_str_copy(&mp_type_str, (const byte *)"_const_str_out1",15);
    //     // printf("before m_new_obj \n");
    //     // printf("before sizeof(mp_obj_str_t) %d \n",sizeof(mp_obj_str_t) );
    //     size_t len_mp_obj_str_t=sizeof(mp_obj_str_t) * (1);
    //     // printf("before sizeof(mp_obj_str_t) * (1) %d \n",len_mp_obj_str_t);
    //     //     void *ptr = malloc(len_mp_obj_str_t);
    //     //     printf("after malloc \n");
    //     //     printf("after malloc %d && %d \n",(int)(ptr == NULL),(int)(malloc(len_mp_obj_str_t) != 0));

    //     //     if (ptr == NULL && malloc(len_mp_obj_str_t) != 0) {
    //     //         printf("before m_malloc_fail!!!!!!!!!!!!!!!!!\n");
    //     //         m_malloc_fail(malloc(len_mp_obj_str_t));
    //     //     }

    //     //     printf("malloc %d : %p\n", malloc(len_mp_obj_str_t), ptr);//malloc 16 : 0x3ffbb4ec

    //     mp_obj_str_t *o = (mp_obj_str_t *)(malloc(len_mp_obj_str_t));
    //     printf("after m_new_obj \n");//after m_new_obj
    //     o->base.type = &mp_type_str;
    //     o->len = 15;
    //         // o->hash = qstr_compute_hash((const byte *)"_const_str_out1", len);
    //         // byte *p = m_new(byte, 15 + 1);
    //         int len_m_byte=sizeof(byte) * (o->len+1);
    //         byte *p =  (byte *)(malloc(len_m_byte));
    //         o->data = p;
    //         printf("after byte o->data \n");
    //         memcpy(p, (const byte *)"_const_str_out1", o->len * sizeof(byte));
    //         p[o->len] = '\0'; // for now we add null for compatibility with C ASCIIZ strings
        
    // printf("before mp_obj_test before while \n");
    // mp_obj_t mp_obj_test1= MP_OBJ_FROM_PTR(o);
    // // }
    // // mp_obj_t mp_obj_test1=mp_obj_new_str("_const_str_out1",15);
    // printf("after mp_obj_res \n");
    // printf("Test111111111111-------,mp_obj_test=%d \n",(int)mp_obj_test1);
    // printf("Test111111-------,const char *str = %s \n",mp_obj_str_get_str(mp_obj_test1));

    while (1)
    {
        if(buf){
            // feed buf
            cnt_codes=_feed_buf();
            printf("after _feed_buf \n");

            if(cnt_codes>0){
                if(str_decode_join || len_str_decode_join>0){
                    if(*str_decode_join){
                        free(str_decode_join);
                        str_decode_join=NULL;
                        len_str_decode_join=0;
                    }
                }
            }

            // decode
            char *str_res_long= _decode_qrcode(cnt_codes,&str_decode_join,&len_str_decode_join);

            // micropython callback
            printf("str_decode_join %d ,is NULL %d ,len = %d \n",(int)str_decode_join,(int)(!str_decode_join),len_str_decode_join);
            // printf("str_res_long %d ,is NULL %d ,len = %d \n",(int)str_res_long,(int)(!str_res_long),strlen(str_res_long));
            if(str_decode_join)printf("*str_decode_join %d \n",(int)(*str_decode_join));
            printf("mp_cb_decode %d ,mp_obj_is_callable(mp_cb_decode) %d \n",(int)mp_cb_decode,(int)mp_obj_is_callable(mp_cb_decode));

            // mp_call_function_0_protected(mp_cb_decode);
            // mp_call_function_0(mp_cb_decode);
            if (mp_cb_decode != mp_const_none && cnt_codes>0 && mp_obj_is_callable(mp_cb_decode) && str_decode_join!=NULL){
                if(str_decode_join)printf("Test222222222-------,str_decode_join=%s ,len = %d \n",str_decode_join,len_str_decode_join);
                // mp_call_function_1_protected(mp_cb_decode, mp_obj_new_str(str_res_long,strlen(str_res_long)));
                mp_obj_t mp_obj_res=mp_obj_new_str_dr(str_decode_join,15);
                printf("after mp_obj_res \n");
                printf("Test3333333-------,mp_obj_res=%d \n",(int)mp_obj_res);
                printf("Test3333333-------,const char *str = %s \n",mp_obj_str_get_str(mp_obj_res));
                // mp_obj_t mp_obj_new_str(const char *data, size_t len) {
                //     qstr q = qstr_find_strn(data, len);
                //     if (q != MP_QSTRnull) {
                //         // qstr with this data already exists
                //         return MP_OBJ_NEW_QSTR(q);
                //     } else {
                //         // no existing qstr, don't make one
                //         return mp_obj_new_str_copy(&mp_type_str, (const byte *)data, len);
                //     }
                // }
                mp_sched_schedule(mp_cb_decode, mp_obj_res);
            }
            if(str_res_long)printf("Test444444-------,str_res_long=%s ,len = %d \n",str_res_long,len_str_decode_join);
            // printf("before vTaskSuspend %d \n",(int)handle_xtask_decode);
            buf=NULL;
            printf("after buf=NULL\n");
        }
        vTaskSuspend(NULL);
        // vTaskDelay( xDelay );
    }
// fail_qr_resize:
// fail_qr:

    // 清除xTask
    vTaskDelete(handle_xtask_decode);
}



// -------------c 处理结束

// -------------module 直接使用的接口
// micropython接口，设置宽高，//？必须先设定？
STATIC mp_obj_t mp_init_quirc(mp_obj_t in_int_w,mp_obj_t in_int_h)//const uint8_t *fb
{
    if(!_init_quirc(mp_obj_get_int(in_int_w),mp_obj_get_int(in_int_h)))return mp_const_false;
    else return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(dr_mp_init_obj, mp_init_quirc);
 
//micropython接口，传入num_codes
STATIC mp_obj_t decode_qrcode(mp_obj_t in_cnt_codes)//const uint8_t *fb
{
    int num_codes = mp_obj_get_int(in_cnt_codes);
    // _decode_qrcode(num_codes);
    bool is_decode_suc=true;
    return is_decode_suc?mp_const_true:mp_const_false;//mp_obj_new_tuple(num_codes, out_tuple);    
}
// Define a Python reference to the function above.
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dr_mp_quirc_decode_obj, decode_qrcode);

//micropython接口，传入bytes,图像，cbFuntion;宽，高 之前设定，或type quirc设定，或默认值
STATIC mp_obj_t feed_buf(const mp_obj_t in_mp_obj_fb,mp_obj_t mp_cb_decode)//const uint8_t *fb
{   
//#ifdef CONFIG_ESP32CAM
//    camera_fb_t *fb = NULL;
//    fb = esp_camera_fb_get();
//    if (!fb) {
//        printf("Camera in qrcode Camera capture failed\n");
//        // ESP_LOGE(TAG, "Camera capture failed");
//    }
//    buf= fb->buf;
//    in_w = fb->width;
//    in_h = fb->height;
//    len_buf=fb->len;
//#else
    // 从micorypython传回b""类型的图片数据，转成uint8_t *
    GET_STR_DATA_LEN(in_mp_obj_fb, byte_buf, len_byte_buf);
    // 赋予静态变量
    buf=(uint8_t *)byte_buf;
    len_buf=len_byte_buf;
    // printBuf(buf,len_buf);    
//#endif
    //if(handle_xtask_decode){
    //    //vTaskResume(handle_xtask_decode);
    //    //printf("vTaskResume %d \n",(int)handle_xtask_decode);
    //}
    // mp_obj_t out_res = _feed_buf();//buf,len_buf,in_w,in_h
//#ifdef CONFIG_ESP32CAM
//    esp_camera_fb_return(fb);
//#endif
    // return out_res;
    return mp_const_none;
}
// Define a Python reference to the function above.
STATIC MP_DEFINE_CONST_FUN_OBJ_2(dr_mp_feed_buf_obj, feed_buf);


// 清理quirc函数
STATIC mp_obj_t close_quirc()
{
    printf("quirc close=> !qr %d \n",(!qr_global));
    if (!qr_global) {        
        return mp_const_false;
    }
    quirc_destroy(qr_global);
    qr_global=NULL;
    printf("quirc close=> success \n");
    // 清除xTask
    vTaskDelete(handle_xtask_decode);
    handle_xtask_decode=NULL;
// #ifdef CONFIG_ESP32CAM
    // esp_err_t err = esp_camera_deinit();
	// if (err != ESP_OK) 
	// {
		// ESP_LOGE(TAG, "Camera deinit failed");
		// return mp_const_false;
	// }
	// return mp_const_true;
// #endif
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(dr_mp_close_quirc, close_quirc);

///////////////////////////////////定义quirc type
// c用到的数据，用来处理对应的uPy数据；传入buf所需的结构体
typedef struct _mp_obj_quirc_t
{
    mp_obj_base_t base;
    uint8_t *buf;
    size_t len_buf;
    int in_w,in_h;
    mp_obj_t mp_cb_decode;
} mp_obj_quirc_t;
const mp_obj_type_t mp_obj_quirc_type;

//quirc的成员函数 
STATIC mp_obj_t quirc_feed(mp_obj_t self_in , const mp_obj_t in_mp_obj_fb)//,mp_obj_t in_mp_cb_decode 
{
    //if(in_mp_cb_decode != mp_const_none){ //可选第三个参数，cbFunction
    //    self->mp_cb_decode=in_mp_cb_decode;
    //}
    //// 保存到全局
    //mp_cb_decode=self->mp_cb_decode;

    mp_obj_quirc_t *self = MP_OBJ_TO_PTR(self_in);  //从第一个参数中提取对象指针
    if(!buf){
        GET_STR_DATA_LEN(in_mp_obj_fb, byte_buf, len_byte_buf);
        // 赋予静态变量
        buf=(uint8_t *)byte_buf;
        len_buf=len_byte_buf;
        if(handle_xtask_decode){
            vTaskResume(handle_xtask_decode);
            printf("vTaskResume %d \n",(int)handle_xtask_decode);
        }
        return mp_const_true;
    }
    return mp_const_false;//feed_buf(in_mp_obj_fb,self->mp_cb_decode);
} 
STATIC MP_DEFINE_CONST_FUN_OBJ_2(quirc_feed_obj,quirc_feed);
////////////////////如果quirc有成员函数，定义完成

////////////////////begin----type的基本定义要求
// 定义type的locals_dict_type 
STATIC const mp_rom_map_elem_t quirc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_feed), MP_ROM_PTR(&quirc_feed_obj) },
}; 
//定义字典的宏 
STATIC MP_DEFINE_CONST_DICT(quirc_locals_dict,quirc_locals_dict_table);
// 定义构造函数
STATIC mp_obj_t quirc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, 3, true);// 检查参数个数，最少1个参数，最多3个参数
    mp_obj_quirc_t *self = m_new_obj(mp_obj_quirc_t);
    self->base.type = &mp_obj_quirc_type;
    //// 从micorypython传回b""类型的图片数据，转成uint8_t *
    //GET_STR_DATA_LEN(args[0], byte_buf, len_byte_buf);
    //// 赋予静态变量
    //self->buf=(uint8_t *)byte_buf;
    //self->len_buf=len_byte_buf;
    self->in_w = mp_obj_get_int(args[0]);//识别图像缩放宽
    self->in_h = mp_obj_get_int(args[1]);//识别图像缩放高
    printf("quirc_make_new _init_quirc %d \n",(int)_init_quirc(self->in_w,self->in_h));

    if(n_args==3 && args[2] != mp_const_none){ //可选第三个参数，cbFunction
        self->mp_cb_decode=args[2];
    }else self->mp_cb_decode = mp_const_none;
    // 保存到全局
    mp_cb_decode=self->mp_cb_decode;
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
    
    // { MP_ROM_QSTR(MP_QSTR___del__),  MP_ROM_PTR(&dr_mp_close_quirc) },

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&dr_mp_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_feed_buf), MP_ROM_PTR(&dr_mp_feed_buf_obj) },
    { MP_ROM_QSTR(MP_QSTR_quirc_decode), MP_ROM_PTR(&dr_mp_quirc_decode_obj) },
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