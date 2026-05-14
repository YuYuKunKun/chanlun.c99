/*
 * MIT License
 *
 * Copyright (c) 2024 YuYuKunKun
 *
 * chan_wrapper.h — English-named C wrapper API for Python bindings.
 * All functions use extern "C" linkage so ctypes can load them.
 * Each one forwards to the Chinese-named C API in chan.h/chan.c.
 */

#ifndef CHAN_WRAPPER_H
#define CHAN_WRAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * OOM / fatal error handler
 * ================================================================ */

typedef void (*chan_fatal_callback)(const char* msg);
void chan_set_fatal_callback(chan_fatal_callback cb);

/* ================================================================
 * Memory debug
 * ================================================================ */

void chan_memory_summary(void);
int  chan_allocation_count(int type_tag);
int  chan_free_count(int type_tag);
void chan_set_debug_mem(int enabled);

/* ================================================================
 * Config
 * ================================================================ */

void* chan_config_new(void);
void* chan_config_new_no_push(void);
void  chan_config_release(void* config);

void        chan_config_set_bool(void* config, const char* field, int value);
void        chan_config_set_int(void* config, const char* field, int value);
void        chan_config_set_double(void* config, const char* field, double value);
void        chan_config_set_string(void* config, const char* field, const char* value);
int         chan_config_get_bool(void* config, const char* field);
int         chan_config_get_int(void* config, const char* field);
double      chan_config_get_double(void* config, const char* field);
const char* chan_config_get_string(void* config, const char* field);

/* ================================================================
 * K-line creation
 * ================================================================ */

void* chan_kline_new(const char* id, int index, int period, time_t ts,
                     double open, double high, double low, double close, double vol);
void* chan_kline_create_plain(const char* id, time_t ts,
                              double open, double high, double low, double close,
                              double vol, int index, int period);

/* ================================================================
 * Observer lifecycle
 * ================================================================ */

void* chan_observer_new(const char* symbol, int period, void* config);
void  chan_observer_feed_kline(void* obs, void* kline);
void* chan_observer_read_file(const char* path, void* config);
void  chan_observer_release(void* obs);

/* ================================================================
 * Observer sequence lengths
 * ================================================================ */

size_t chan_observer_raw_klines_length(void* obs);
size_t chan_observer_chan_klines_length(void* obs);
size_t chan_observer_base_chan_klines_length(void* obs);
size_t chan_observer_fractals_length(void* obs);
size_t chan_observer_strokes_length(void* obs);
size_t chan_observer_stroke_hubs_length(void* obs);
size_t chan_observer_segments_length(void* obs);
size_t chan_observer_hubs_length(void* obs);
size_t chan_observer_ext_segments_length(void* obs);
size_t chan_observer_ext_hubs_length(void* obs);
size_t chan_observer_ext_segments_seg_length(void* obs);
size_t chan_observer_ext_hubs_seg_length(void* obs);
size_t chan_observer_seg_seg_length(void* obs);
size_t chan_observer_seg_hubs_length(void* obs);
size_t chan_observer_ext_seg_extseg_length(void* obs);
size_t chan_observer_ext_hubs_extseg_length(void* obs);

/* ================================================================
 * Observer sequence element access
 *   Returns borrowed pointer (no refcount change).
 *   Returns NULL if index is out of bounds.
 * ================================================================ */

void* chan_observer_raw_klines_at(void* obs, size_t idx);
void* chan_observer_chan_klines_at(void* obs, size_t idx);
void* chan_observer_base_chan_klines_at(void* obs, size_t idx);
void* chan_observer_fractals_at(void* obs, size_t idx);
void* chan_observer_strokes_at(void* obs, size_t idx);
void* chan_observer_stroke_hubs_at(void* obs, size_t idx);
void* chan_observer_segments_at(void* obs, size_t idx);
void* chan_observer_hubs_at(void* obs, size_t idx);
void* chan_observer_ext_segments_at(void* obs, size_t idx);
void* chan_observer_ext_hubs_at(void* obs, size_t idx);
void* chan_observer_ext_segments_seg_at(void* obs, size_t idx);
void* chan_observer_ext_hubs_seg_at(void* obs, size_t idx);
void* chan_observer_seg_seg_at(void* obs, size_t idx);
void* chan_observer_seg_hubs_at(void* obs, size_t idx);
void* chan_observer_ext_seg_extseg_at(void* obs, size_t idx);
void* chan_observer_ext_hubs_extseg_at(void* obs, size_t idx);

/* ================================================================
 * K-line field accessors
 * ================================================================ */

const char* chan_kline_id(void* k);
int         chan_kline_index(void* k);
int         chan_kline_period(void* k);
time_t      chan_kline_timestamp(void* k);
double      chan_kline_high(void* k);
double      chan_kline_low(void* k);
double      chan_kline_open(void* k);
double      chan_kline_close(void* k);
double      chan_kline_volume(void* k);
int         chan_kline_direction(void* k);

/* ================================================================
 * Chan K-line (缠论K线) field accessors
 * ================================================================ */

int         chan_ckline_index(void* ck);
time_t      chan_ckline_timestamp(void* ck);
double      chan_ckline_high(void* ck);
double      chan_ckline_low(void* ck);
int         chan_ckline_direction(void* ck);
int         chan_ckline_fractal_type(void* ck);
double      chan_ckline_fractal_value(void* ck);
int         chan_ckline_period(void* ck);
const char* chan_ckline_id(void* ck);
int         chan_ckline_orig_start(void* ck);
int         chan_ckline_orig_end(void* ck);
void*       chan_ckline_ref_kline(void* ck);

/* ================================================================
 * Fractal (分型) field accessors
 * ================================================================ */

void*   chan_fractal_left(void* f);
void*   chan_fractal_mid(void* f);
void*   chan_fractal_right(void* f);
int     chan_fractal_structure(void* f);
time_t  chan_fractal_timestamp(void* f);
double  chan_fractal_feature_value(void* f);

/* ================================================================
 * Dash line (虚线: strokes & segments) accessors
 * ================================================================ */

int         chan_dash_index(void* d);
const char* chan_dash_id(void* d);
int         chan_dash_level(void* d);
void*       chan_dash_wen(void* d);
void*       chan_dash_wu(void* d);
int         chan_dash_direction(void* d);
double      chan_dash_high(void* d);
double      chan_dash_low(void* d);
int         chan_dash_is_valid(void* d);
const char* chan_dash_mode(void* d);

/* Dash base-sequence (constituent strokes for a segment, empty for a stroke) */
size_t chan_dash_base_seq_length(void* d);
void*  chan_dash_base_seq_at(void* d, size_t idx);

/* Dash feature sequence (线段特征) */
size_t chan_dash_feature_seq_length(void* d);
void*  chan_dash_feature_seq_at(void* d, size_t idx);

/* Dash internal hub sequences */
size_t chan_dash_real_hub_seq_length(void* d);
void*  chan_dash_real_hub_seq_at(void* d, size_t idx);
size_t chan_dash_virtual_hub_seq_length(void* d);
void*  chan_dash_virtual_hub_seq_at(void* d, size_t idx);
size_t chan_dash_combined_hub_seq_length(void* d);
void*  chan_dash_combined_hub_seq_at(void* d, size_t idx);

/* ================================================================
 * Hub (中枢) accessors
 * ================================================================ */

int         chan_hub_index(void* h);
const char* chan_hub_id(void* h);
int         chan_hub_level(void* h);
double      chan_hub_high(void* h);
double      chan_hub_low(void* h);
double      chan_hub_high_high(void* h);
double      chan_hub_low_low(void* h);
int         chan_hub_direction(void* h);
int         chan_hub_is_complete(void* h);
const char* chan_hub_status(void* h);
void*       chan_hub_third_buy_sell(void* h);
void*       chan_hub_local_third_buy_sell(void* h);
size_t      chan_hub_element_count(void* h);
void*       chan_hub_element_at(void* h, size_t idx);

/* ================================================================
 * Segment feature (线段特征) accessors
 * ================================================================ */

int    chan_segfeat_index(void* sf);
const char* chan_segfeat_id(void* sf);
int    chan_segfeat_direction(void* sf);
void*  chan_segfeat_wen(void* sf);
void*  chan_segfeat_wu(void* sf);
double chan_segfeat_high(void* sf);
double chan_segfeat_low(void* sf);
size_t chan_segfeat_element_count(void* sf);
void*  chan_segfeat_element_at(void* sf, size_t idx);

/* ================================================================
 * Gap (缺口) accessors
 * ================================================================ */

double chan_gap_high(void* g);
double chan_gap_low(void* g);

/* ================================================================
 * Reference counting
 *   chan_retain  -> C 引用()  (increment refcount)
 *   chan_release -> C 解引用() (decrement, free if zero)
 * ================================================================ */

void chan_retain(void* obj);
void chan_release(void* obj);

/* ================================================================
 * Utility
 * ================================================================ */

time_t chan_str2timestamp(const char* ts_str);

/* ================================================================
 * Bulk K-line data export
 *   Allocates and fills a flat double array:
 *     [ts, open, high, low, close, volume] * N
 *   Returns count via out_count. Caller must free().
 * ================================================================ */

double* chan_observer_raw_klines_export(void* obs, size_t* out_count);

/* ================================================================
 * Direction / structure helpers
 * ================================================================ */

int chan_direction_is_up(int dir);
int chan_direction_is_down(int dir);
int chan_direction_is_gap(int dir);
int chan_direction_is_containment(int dir);
int chan_direction_is_junction(int dir);
int chan_direction_flip(int dir);

/* ================================================================
 * 虚线 (Dash) analysis methods
 * ================================================================ */

int  chan_dash_before(void* d, void* other);
int  chan_dash_after(void* d, void* other);

/* ================================================================
 * 线段 (Segment) analysis methods
 * ================================================================ */

const char* chan_segment_sixiang(void* seg);
int         chan_segment_feature_fractal_end(void* seg);
void        chan_segment_feature_seq_state(void* seg, int* left, int* mid, int* right);
void*       chan_segment_get_gap(void* seg);
void*       chan_segment_find_penetration(void* seg);

/* ================================================================
 * 笔 (Stroke) analysis — observer-level lookup
 * ================================================================ */

void* chan_observer_find_stroke_by_wen(void* obs, void* fractal);
void* chan_observer_find_stroke_by_wu(void* obs, void* fractal);
void* chan_observer_find_stroke_by_ckline(void* obs, void* ck, int offset);
int   chan_stroke_relative_relation(void* stroke, void* config);

/* ================================================================
 * 背驰分析 (Divergence analysis)
 * ================================================================ */

int chan_divergence_slope(void* enter, void* leave);
int chan_divergence_measure(void* enter, void* leave);
int chan_divergence_macd(void* enter, void* leave, void** klines, size_t n, const char* method);
int chan_divergence_full(void* enter, void* leave, void** klines, size_t n);
int chan_divergence_any(void* enter, void* leave, void** klines, size_t n);
int chan_divergence_config(void* enter, void* leave, void** klines, size_t n, void* config);
int chan_divergence_optional(void* enter, void* leave, void** klines, size_t n);
int chan_divergence_mode(void* enter, void* leave, void** klines, size_t n, void* config, const char* mode);

/* Helper: extract K-line pointer array for a dash from the observer */
int  chan_observer_get_kline_array(void* obs, void* dash, void*** out, size_t* out_len);
void chan_free_kline_array(void** arr);

/* ================================================================
 * 虚线 (Dash) field accessors
 * ================================================================ */

void* chan_dash_confirm_ckline(void* d);
void* chan_dash_prev_gap(void* d);
void* chan_dash_prev_end(void* d);
int   chan_dash_short_circuit(void* d);

/* ================================================================
 * 虚线 creation
 * ================================================================ */

void* chan_dash_create_stroke(void* wen, void* wu, int valid);

/* ================================================================
 * 虚线 K-line extraction (returns malloc'd array, caller frees)
 * ================================================================ */

int chan_dash_get_raw_klines(void* dash, void* obs, void*** out, size_t* out_len);
int chan_dash_get_chan_klines(void* dash, void* obs, void*** out, size_t* out_len);

/* ================================================================
 * 笔 utility functions — operate on observer's internal sequences
 * ================================================================ */

int   chan_observer_stroke_chan_count(void* obs, void* config);
void* chan_observer_stroke_second_high(void* obs, int same_end);
void* chan_observer_stroke_second_low(void* obs, int same_end);
void* chan_observer_stroke_actual_high(void* obs, int same_end);
void* chan_observer_stroke_actual_low(void* obs, int same_end);

/* ================================================================
 * Pipeline analysis — re-run analysis on observer's sequences
 * ================================================================ */

void chan_observer_analyze_strokes(void* obs, void* config);
void chan_observer_analyze_segments(void* obs, void* config);
void chan_observer_analyze_ext_segments(void* obs, void* config);
void chan_observer_analyze_stroke_hubs(void* obs, int skip_first, const char* id);
void chan_observer_analyze_segment_hubs(void* obs, int skip_first, const char* id);

#ifdef __cplusplus
}
#endif
#endif /* CHAN_WRAPPER_H */
