// Include MicroPython API.
#include "py/obj.h" //MP_ROM_QSTR, MP_ROM_QSTR
#include "py/runtime.h"
#include "py/objlist.h" //mp_obj_new_list
#include "py/objstr.h" // GET_STR_DATA_LEN
#include "py/builtin.h" // Many a time you will also need py/builtin.h, where the python built-in functions and modules are declared
#include <string.h> //for memcpy
// esp?
// #include "esp_heap_caps.h"
#ifndef DR_DEBUG_PRINT
#define DR_DEBUG_PRINT 1
#endif
STATIC int is_debug=0;
#ifdef DR_DEBUG_PRINT
#define DEBUG_printf(...) !(is_debug>1)?0:printf(__VA_ARGS__)//ESP_LOGI("modCamera:", __VA_ARGS__)
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
#else
#define DEBUG_printf(...) (void)0
#endif

#define LIST_MIN_ALLOC_DR 4
STATIC void *m_malloc_dr(size_t num_bytes) {
    void *ptr = malloc(num_bytes);
#ifdef CONFIG_ESP32CAM
    //失败尝试用SPIRAM
    if (ptr == NULL && num_bytes != 0)
    {
        DEBUG_printf("!!!!!!!!!!!!! m_malloc_dr.malloc fail \n try heap_caps_malloc...\n %u bytes, heap free in SPIRAM = %d \n", (uint)num_bytes,heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        ptr = heap_caps_malloc(num_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);        
    }
#endif
    if (ptr == NULL && num_bytes != 0) {
        // DEBUG_printf("!!!!!!!!!!!!! m_malloc_dr.malloc fail \n memory allocation failed, allocating %u bytes\n", (uint)num_bytes);
        // #if MICROPY_ENABLE_GC
        // if (gc_is_locked()) {
        //     mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("memory allocation failed, heap is locked"));
        // }
        // #endif
        mp_raise_msg_varg(&mp_type_MemoryError,
            MP_ERROR_TEXT("memory allocation failed, allocating %u bytes,\n heap free in SPIRAM = %d"), (uint)num_bytes,heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));        
    }
    #if MICROPY_MEM_STATS
    MP_STATE_MEM(total_bytes_allocated) += num_bytes;
    MP_STATE_MEM(current_bytes_allocated) += num_bytes;
    UPDATE_PEAK();
    #endif
    // DEBUG_printf("m_malloc_dr %d : %p\n", num_bytes, ptr);
    return ptr;
}
#define m_new_dr(type, num) ((type *)(m_malloc_dr(sizeof(type) * (num))))
#define m_new_obj_dr(type) (m_new_dr(type, 1))
#define mp_seq_clear_dr(start, len, alloc_len, item_sz) memset((byte *)(start) + (len) * (item_sz), 0, ((alloc_len) - (len)) * (item_sz))

// // 解决mp_obj_new_str的bug，把功能层层展开
// STATIC mp_obj_t mp_obj_new_str_dr(const char *data, size_t len)
// {
//     // printf("begin mp_obj_new_str_dr \n");
//     // printf("begin mp_obj_new_str_dr %p %d \n",data,len);
//     size_t len_mp_obj_str_t=sizeof(mp_obj_str_t) * (1);
//     // printf("mp_obj_new_str_dr %d \n",len_mp_obj_str_t);
//     mp_obj_str_t *o = (mp_obj_str_t *)(malloc(len_mp_obj_str_t));
//     if (o == NULL && len_mp_obj_str_t != 0) {
//         printf("!!!!!!!!!!!!!mp_obj_new_str_dr.malloc fail \n");
//         m_malloc_fail(len_mp_obj_str_t);
//     }
//     o->base.type = &mp_type_str;
//     o->len = len;
//     if(data){
//         // o->hash = qstr_compute_hash((const byte *)"_const_str_out1", len);
//         // byte *p = m_new(byte, 15 + 1);
//         int len_m_byte=sizeof(byte) * (len+1);
//         byte *p = (byte *)(malloc(len_m_byte));
//         o->data = p;
//         // printf("after byte o->data \n");
//         memcpy(p, (const byte *)data, len * sizeof(byte));
//         p[len] = '\0'; // for now we add null for compatibility with C ASCIIZ strings
//     }
//     // printf("all done mp_obj_test before MP_OBJ_FROM_PTR \n");
//     return MP_OBJ_FROM_PTR(o);
// }
// 解决mp_obj_new_str的bug，把功能层层展开
STATIC mp_obj_t mp_obj_new_str_copy_dr(const mp_obj_type_t *type, const byte *data, size_t len) {
    mp_obj_str_t *o = m_new_obj_dr(mp_obj_str_t);
    o->base.type = &mp_type_bytes;
    o->len = len;
    byte *p = m_new_dr(byte, len + 1);
    o->data = p;
    if(data){
        o->hash = qstr_compute_hash((const byte *)"_const_str_out1", len);
        // DEBUG_printf("after byte o->data \n");
        memcpy(p, (const byte *)data, len * sizeof(byte));
    }
    p[len] = '\0'; // for now we add null for compatibility with C ASCIIZ strings
    // DEBUG_printf("all done mp_obj_test before MP_OBJ_FROM_PTR \n");
    return MP_OBJ_FROM_PTR(o);
}
STATIC mp_obj_t mp_obj_new_str_dr(const char *data, size_t len){
    return mp_obj_new_str_copy_dr(&mp_type_str, (const byte *)data, len);
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

// #ifndef CONFIG_ESP32CAM
// typedef enum {
//     FRAMESIZE_96X96,    // 96x96
//     FRAMESIZE_QQVGA,    // 160x120
//     FRAMESIZE_QCIF,     // 176x144
//     FRAMESIZE_HQVGA,    // 240x176
//     FRAMESIZE_240X240,  // 240x240
//     FRAMESIZE_QVGA,     // 320x240
//     FRAMESIZE_CIF,      // 400x296
//     FRAMESIZE_HVGA,     // 480x320
//     FRAMESIZE_VGA,      // 640x480
//     FRAMESIZE_SVGA,     // 800x600
//     FRAMESIZE_XGA,      // 1024x768
//     FRAMESIZE_HD,       // 1280x720
//     FRAMESIZE_SXGA,     // 1280x1024
//     FRAMESIZE_UXGA,     // 1600x1200
//     // 3MP Sensors
//     FRAMESIZE_FHD,      // 1920x1080
//     FRAMESIZE_P_HD,     //  720x1280
//     FRAMESIZE_P_3MP,    //  864x1536
//     FRAMESIZE_QXGA,     // 2048x1536
//     // 5MP Sensors
//     FRAMESIZE_QHD,      // 2560x1440
//     FRAMESIZE_WQXGA,    // 2560x1600
//     FRAMESIZE_P_FHD,    // 1080x1920
//     FRAMESIZE_QSXGA,    // 2560x1920
//     FRAMESIZE_INVALID
// } framesize_t;
// typedef enum {
//     ASPECT_RATIO_4X3,
//     ASPECT_RATIO_3X2,
//     ASPECT_RATIO_16X10,
//     ASPECT_RATIO_5X3,
//     ASPECT_RATIO_16X9,
//     ASPECT_RATIO_21X9,
//     ASPECT_RATIO_5X4,
//     ASPECT_RATIO_1X1,
//     ASPECT_RATIO_9X16
// } aspect_ratio_t;
// typedef struct {
//         const uint16_t width;
//         const uint16_t height;
//         const aspect_ratio_t aspect_ratio;
// } resolution_info_t;
// const resolution_info_t resolution[FRAMESIZE_INVALID] = {
//     {   96,   96, ASPECT_RATIO_1X1   }, /* 96x96 */
//     {  160,  120, ASPECT_RATIO_4X3   }, /* QQVGA */
//     {  176,  144, ASPECT_RATIO_5X4   }, /* QCIF  */
//     {  240,  176, ASPECT_RATIO_4X3   }, /* HQVGA */
//     {  240,  240, ASPECT_RATIO_1X1   }, /* 240x240 */
//     {  320,  240, ASPECT_RATIO_4X3   }, /* QVGA  */
//     {  400,  296, ASPECT_RATIO_4X3   }, /* CIF   */
//     {  480,  320, ASPECT_RATIO_3X2   }, /* HVGA  */
//     {  640,  480, ASPECT_RATIO_4X3   }, /* VGA   */
//     {  800,  600, ASPECT_RATIO_4X3   }, /* SVGA  */
//     { 1024,  768, ASPECT_RATIO_4X3   }, /* XGA   */
//     { 1280,  720, ASPECT_RATIO_16X9  }, /* HD    */
//     { 1280, 1024, ASPECT_RATIO_5X4   }, /* SXGA  */
//     { 1600, 1200, ASPECT_RATIO_4X3   }, /* UXGA  */
//     // 3MP Sensors
//     { 1920, 1080, ASPECT_RATIO_16X9  }, /* FHD   */
//     {  720, 1280, ASPECT_RATIO_9X16  }, /* Portrait HD   */
//     {  864, 1536, ASPECT_RATIO_9X16  }, /* Portrait 3MP   */
//     { 2048, 1536, ASPECT_RATIO_4X3   }, /* QXGA  */
//     // 5MP Sensors
//     { 2560, 1440, ASPECT_RATIO_16X9  }, /* QHD    */
//     { 2560, 1600, ASPECT_RATIO_16X10 }, /* WQXGA  */
//     { 1088, 1920, ASPECT_RATIO_9X16  }, /* Portrait FHD   */
//     { 2560, 1920, ASPECT_RATIO_4X3   }, /* QSXGA  */
// };
// #endif

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
        // int mem_need=(obj_quirc.fb.width*obj_quirc.fb.height >= 320*240) ? (80*1024)+8896 : 30*1024;//8896是quirc.payload的数组长度
        int mem_need=(obj_quirc.fb.width*obj_quirc.fb.height >= 320*240) ? (30*1024): 30*1024;//8896是quirc.payload的数组长度
        mem_need += QUIRC_MAX_BITMAP + QUIRC_MAX_PAYLOAD;

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
        DEBUG_printf("xTask begin %p \n",obj_quirc.handle_xtask_decode);
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
    !(is_debug>1)?0:printBuf(image,obj_quirc.fb.len,"before quirc_end image");
    quirc_end(obj_quirc.qr);
    // 打印检查
    !(is_debug>1)?0:printBuf(image,obj_quirc.fb.len,"after quirc_end image");
    int num_codes = quirc_count(obj_quirc.qr);
    !(is_debug>0)?0:printf("read cnt_codes in buf----------------------> %d \n", num_codes);

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

                // list_str_res[i]=heap_caps_malloc(strlen(quirc_strerror(err)), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                list_str_res[i]=m_malloc_dr(strlen(quirc_strerror(err)));
                strcpy(list_str_res[i],(char *)quirc_strerror(err));
                DEBUG_printf("Quirc DECODE:FAILED list_str_res: %s\n", list_str_res[i]);

            }else{
                len_all+=data.payload_len;

                // list_str_res[i]=heap_caps_malloc(data.payload_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                list_str_res[i]=m_malloc_dr(data.payload_len);
                strcpy(list_str_res[i],(char *)data.payload);
                DEBUG_printf("Quirc DECODE:OK Data list_str_res: %s\n", list_str_res[i]);
            }
        }
        return len_all;
    }
    return 0;
}

// 触发micropython的回调函数
STATIC bool _cb_fun_decode_res(mp_obj_t mp_cb_decode,char ** list_str_res,int cnt_codes,int len_str_res_join){
    bool is_ready_to_callback=mp_cb_decode!=NULL && mp_cb_decode != mp_const_none && mp_obj_is_callable(mp_cb_decode);
    bool is_decode_success=list_str_res && len_str_res_join>0 && cnt_codes>0;
    // micropython callback
    DEBUG_printf("xTask:_cb_fun_decode_res: is_ready_to_callback=%d ,is_decode_success =%d \n",is_ready_to_callback,is_decode_success);
    if (is_ready_to_callback){
        if(is_decode_success)
        {
            // 先生成，并及时释放list_str_res
            mp_obj_t list_mp_str_res[cnt_codes];
            for(int i=0;i<cnt_codes;i++){
                list_mp_str_res[i]=is_ready_to_callback ? mp_obj_new_str_dr((const char *)list_str_res[i],strlen(list_str_res[i])) : NULL;
                free(list_str_res[i]);
            }
            // mp_call_function_1_protected(mp_cb_decode, mp_obj_new_str(str_res_long,strlen(str_res_long)));
            mp_sched_schedule(mp_cb_decode, mp_obj_new_list_dr(cnt_codes, list_mp_str_res));
            return true;
        }
        else
        {
            mp_sched_schedule(mp_cb_decode, mp_const_none);
            return true;
        }
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
            DEBUG_printf("xTask:before _feed_buf obj_quirc.fb.buf != NULL \n");
            int cnt_codes=_feed_buf();
            DEBUG_printf("xTask:after _feed_buf=%d \n",cnt_codes);
            if(cnt_codes>0){
                // decode
                char *list_str_res[cnt_codes];
                int len_str_res_join= _decode_qrcode(list_str_res,cnt_codes);//返回所有字符串连接后的总长度
                // callback
                _cb_fun_decode_res(obj_quirc.mp_cb_decode,list_str_res,cnt_codes,len_str_res_join);
            }else{
                _cb_fun_decode_res(obj_quirc.mp_cb_decode,NULL,cnt_codes,0);
            }
            obj_quirc.fb.buf=NULL;
            DEBUG_printf("xTask:after buf=NULL \n");
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
        !(is_debug>1)?0:printBuf(byte_buf,len_byte_buf,"byte_buf receive=>");
        obj_quirc.fb.buf=(uint8_t *)byte_buf;
        obj_quirc.fb.len=(size_t)len_byte_buf;
        !(is_debug>1)?0:printBuf(obj_quirc.fb.buf,obj_quirc.fb.len,"obj_quirc.buf got=>");
        if(obj_quirc.handle_xtask_decode){
            vTaskResume(obj_quirc.handle_xtask_decode);
            DEBUG_printf("vTaskResume %p \n",obj_quirc.handle_xtask_decode);
        }else{
            close_quirc();
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("quirc.xTask handle NULL"));
        }

        return mp_const_true;
//#endif
    }else
    {
        DEBUG_printf("quirc.feed_buf=>obj_quirc.fb.buf = %p \n",obj_quirc.fb.buf);
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
    // const
    // { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_96X96    ), MP_ROM_INT(FRAMESIZE_96X96    )},  // 96x96
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_QQVGA    ), MP_ROM_INT(FRAMESIZE_QQVGA    )},  // 160x120
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_QCIF     ), MP_ROM_INT(FRAMESIZE_QCIF     )},  // 176x144
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_HQVGA    ), MP_ROM_INT(FRAMESIZE_HQVGA    )},  // 240x176
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_240X240  ), MP_ROM_INT(FRAMESIZE_240X240  )},  // 240x240
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_QVGA     ), MP_ROM_INT(FRAMESIZE_QVGA     )},  // 320x240
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_CIF      ), MP_ROM_INT(FRAMESIZE_CIF      )},  // 400x296
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_HVGA     ), MP_ROM_INT(FRAMESIZE_HVGA     )},  // 480x320
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_VGA      ), MP_ROM_INT(FRAMESIZE_VGA      )},  // 640x480
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_SVGA     ), MP_ROM_INT(FRAMESIZE_SVGA     )},  // 800x600
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_XGA      ), MP_ROM_INT(FRAMESIZE_XGA      )},  // 1024x768
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_HD       ), MP_ROM_INT(FRAMESIZE_HD       )},  // 1280x720
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_SXGA     ), MP_ROM_INT(FRAMESIZE_SXGA     )},  // 1280x1024
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_UXGA     ), MP_ROM_INT(FRAMESIZE_UXGA     )},  // 1600x1200
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_FHD      ), MP_ROM_INT(FRAMESIZE_FHD      )},  // 1920x1080
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_P_HD     ), MP_ROM_INT(FRAMESIZE_P_HD     )},  //  720x1280
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_P_3MP    ), MP_ROM_INT(FRAMESIZE_P_3MP    )},  //  864x1536
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_QXGA     ), MP_ROM_INT(FRAMESIZE_QXGA     )},  // 2048x1536
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_QHD      ), MP_ROM_INT(FRAMESIZE_QHD      )},  // 2560x1440
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_WQXGA    ), MP_ROM_INT(FRAMESIZE_WQXGA    )},  // 2560x1600
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_P_FHD    ), MP_ROM_INT(FRAMESIZE_P_FHD    )},  // 1080x1920
	// { MP_ROM_QSTR(MP_QSTR_FRAMESIZE_QSXGA    ), MP_ROM_INT(FRAMESIZE_QSXGA    )},  // 2560x1920
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

    if(n_args>=3)quirc_cb(self,args[2]); //可选第三个参数，cbFunction
    if(n_args>=4){
        self->is_debug=mp_obj_get_int(args[3]); //可选第四个参数，是否debug输出
        is_debug=self->is_debug;
    }

    DEBUG_printf("quirc_make_new _init_quirc return %d \n",(int)is_inited);
    
    
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
