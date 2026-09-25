# -*- coding: utf-8 -*-
"""
视频转码：mp4/avi/... -> .vid（自定义格式，STM32 直接按头播放）
文件布局：
    [ 40 字节文件头 ][ 视频帧1 ][ 音频帧1 ][ 视频帧2 ][ 音频帧2 ] ... (交错存储)
文件头必须和 MCU 端的 Hardware/video_file.h 里的 VideoHeader 严格一致。

修改说明（v2 -> v3）：
    - 强制视频帧率 = TARGET_FPS（通过 ffmpeg fps 过滤器 Drop/Duplicate 帧）
    - 音频视频时长不一致时，自动跳帧对齐（裁掉较长一方的多余内容）
      * 音频比视频长 -> 截断音频
      * 视频比音频长 -> 丢弃尾部视频帧
    - 临时文件自动清理

修改说明（v3 -> v4）：
    - 新增运行时可输入目标分辨率
    - 输入的分辨率自动归一化为：
        * 宽、高为偶数
        * (宽 × 高 × 2) 是 512 的整数倍（一帧字节数可按扇区整除）
    - 分块大小仍然受 CHUNK_MAX_BYTES = 30720 限制

用法：
    python 转码_3_v3原始备份.py
    然后按提示输入视频路径、目标分辨率；输出同目录下的同名 .vid
依赖：
    pip install opencv-python numpy
    音频部分需要 ffmpeg.exe 在 PATH 里（或改 FFMPEG 变量指向它）
"""
import cv2
import numpy as np
import os
import struct
import subprocess
import sys
import tempfile
import math

# Windows 控制台默认是 GBK/936，直接 print 中文加符号容易抛
# UnicodeEncodeError。这里把标准输出强制成 UTF-8，失败就算了。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

# ============================================================================
#  参数（改这里）
# ============================================================================
TARGET_W, TARGET_H = 320, 480      # 默认视频分辨率 —— 运行时可覆盖
TARGET_FPS         = 25            # ★ 目标帧率（强制 25fps，ffmpeg fps 过滤器 Drop/Duplicate）
AUDIO_RATE         = 44100         # 音频采样率，固定 44100（PLLI2S 下精度最好）
AUDIO_CH           = 2             # 声道数
AUDIO_BITS         = 16            # 位深（只支持 16）
CHUNK_MAX_BYTES    = 30720         # ★ 必须 <= 固件 main.c 里的 CHUNK_MAX_BYTES
                                   #   30720=60扇区：320x480(600扇区/帧)→10块/帧；
                                   #   160x128(80扇区/帧)→自动选20480(40扇区)，4块/帧
AUDIO_DMA_ALIGN    = 4             # ★ STM32F4 DMA 要求音频偏移必须 4 字节对齐
FFMPEG = "ffmpeg"                  # ffmpeg 可执行文件路径
VIDEO_MAGIC = 0x31504D56           # "VMP1"

# ============================================================================
#  分辨率归一化：保证宽高为偶数，且一帧字节数是 512 的整数倍
# ============================================================================
def normalize_resolution(w, h):
    """
    输入目标宽高，返回调整后的 (w, h)，满足：
      1. w、h 为正偶数（libx264 yuv420p / RGB565 友好）
      2. w * h * 2 是 512 的整数倍（一帧字节数可按 512 字节扇区整除）
      3. 尽量只微调高度，保持宽度不变
    """
    w = int(w)
    h = int(h)
    if w <= 0 or h <= 0:
        raise ValueError("宽高必须为正整数")

    # 保证偶数
    if w % 2:
        w += 1
    if h % 2:
        h += 1

    # 需要 (w * h * 2) % 512 == 0  =>  (w * h) % 256 == 0
    # 固定 w，调整 h，使 h 是 256 / gcd(w, 256) 的倍数
    g = math.gcd(w, 256)
    need = 256 // g
    if h % need != 0:
        h = ((h + need - 1) // need) * need

    # 再次保证偶数
    if h % 2:
        h += 1

    # 保险校验
    while (w * h * 2) % 512 != 0:
        h += 2

    return w, h

# ============================================================================
#  块参数求解 (仅针对视频帧数据)
# ============================================================================
TARGET_CHUNK_BYTES = CHUNK_MAX_BYTES
def solve_blocks(frame_bytes):
    """返回 (blocks, chunk_bytes)。保证 frame_bytes == blocks * chunk_bytes"""
    total_sectors = frame_bytes // 512
    if frame_bytes % 512:
        raise RuntimeError(f"一帧 {frame_bytes} 字节不是 512 的整数倍，"
                           f"换分辨率（宽必须是 8 的倍数）")
    max_sectors = CHUNK_MAX_BYTES // 512
    cands = [s for s in range(1, max_sectors + 1) if total_sectors % s == 0]
    if not cands:
        raise RuntimeError(f"找不到合法的块大小：一帧 {total_sectors} 个扇区，"
                           f"上限 {max_sectors} 个扇区")
    good = [s for s in cands if s * 512 <= TARGET_CHUNK_BYTES]
    pool = good if good else cands
    sectors = max(pool)
    blocks = total_sectors // sectors
    chunk_bytes = sectors * 512

    if chunk_bytes < 8192:
        print(f"  ⚠ 每块只有 {chunk_bytes} 字节（{sectors} 扇区）—— "
              f"SD 卡单次读太小，吞吐会打折。")
    if blocks < 2:
        print(f"  ⚠ 一帧只有 {blocks} 块 —— 队列里凑不满一帧，"
              f"音视频同步的粒度会变粗")

    video_bps = frame_bytes * TARGET_FPS
    audio_bps = AUDIO_RATE * AUDIO_CH * 2
    total_kbs = (video_bps + audio_bps) / 1024.0
    print(f"  码率   : 视频 {video_bps / 1024:.0f} KB/s + 音频 "
          f"{audio_bps / 1024:.0f} KB/s = {total_kbs:.0f} KB/s")
    if total_kbs > 1200:
        print(f"  ⚠ 合计 {total_kbs:.0f} KB/s 很可能超过你 SD 卡的持续读速度，"
              f"板子上会掉帧、声音会顿。")
    return blocks, chunk_bytes

# ============================================================================
#  用 ffmpeg 抽音频并重采样成裸 PCM
# ============================================================================
def extract_audio(video_path):
    fd, tmp = tempfile.mkstemp(suffix=".pcm")
    os.close(fd)
    cmd = [FFMPEG, "-y", "-i", video_path,
           "-vn", "-f", "s16le", "-acodec", "pcm_s16le",
           "-ar", str(AUDIO_RATE), "-ac", str(AUDIO_CH), tmp]
    try:
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        if r.returncode != 0:
            print("  ffmpeg 抽音频失败（没有音轨？）—— 将生成无音频文件")
            os.remove(tmp)
            return None
        with open(tmp, "rb") as f:
            data = f.read()
        os.remove(tmp)
        if len(data) == 0:
            return None
        return data
    except FileNotFoundError:
        print("  找不到 ffmpeg —— 跳过音频。装好后重跑即可带上声音")
        if os.path.exists(tmp):
            os.remove(tmp)
        return None

# ============================================================================
#  用 ffmpeg 强制视频帧率并缩放分辨率
#  返回临时 MP4 文件路径，调用方负责删除
# ============================================================================
def force_video_to_fps(video_path, target_fps):
    """使用 ffmpeg 的 fps 过滤器强制视频为指定帧率，并缩放到目标分辨率。

    ffmpeg 的 fps 过滤器工作原理：
      - 源帧率 > 目标帧率：按间隔 Drop 帧（如 30->25 每 6 帧 drop 1 帧）
      - 源帧率 < 目标帧率：Duplicate 帧（如 24->25 每隔几帧复制一帧）
      - 输出视频时长与源视频时长基本一致，只是帧密度变了
    """
    fd, tmp = tempfile.mkstemp(suffix=".mp4")
    os.close(fd)

    cmd = [
        FFMPEG, "-y", "-i", video_path,
        "-vf", f"fps={target_fps},scale={TARGET_W}:{TARGET_H}:flags=lanczos",
        "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-r", str(target_fps),
        tmp
    ]
    r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        print("  ffmpeg 视频转码失败（强制帧率）—— 请检查 ffmpeg 是否可用")
        if os.path.exists(tmp):
            os.remove(tmp)
        return None

    # 验证输出文件是否有效
    if os.path.getsize(tmp) == 0:
        print("  ffmpeg 输出文件为空")
        os.remove(tmp)
        return None

    return tmp

# ============================================================================
#  用 ffprobe 获取视频帧数
# ============================================================================
def get_video_frame_count(video_path):
    """用 ffprobe 获取视频的总帧数（适用于 mp4/avi 等容器格式）"""
    cmd = [
        FFMPEG, "-v", "error", "-count_frames",
        "-select_streams", "v:0",
        "-show_entries", "stream=nb_read_frames",
        "-of", "default=noprint_wrappers=1:nokey=1",
        video_path
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if r.returncode == 0 and r.stdout.strip():
            return int(r.stdout.strip())
    except Exception:
        pass
    # ffprobe 不可用时备选：用 OpenCV 逐帧计数
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        return 0
    count = 0
    while True:
        ret, _ = cap.read()
        if not ret:
            break
        count += 1
    cap.release()
    return count

# ============================================================================
def convert(video_path):
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print("无法打开视频"); return

    src_fps = cap.get(cv2.CAP_PROP_FPS) or 0
    cap.release()

    # ========================================================================
    #  步骤1：强制视频为 TARGET_FPS（使用 ffmpeg fps 过滤器）
    # ========================================================================
    print(f"\n步骤1: 强制视频为 {TARGET_FPS}fps（源帧率 {src_fps:.2f}）...")
    temp_video = force_video_to_fps(video_path, TARGET_FPS)
    if temp_video is None:
        print("  视频转码失败，退出")
        return

    # ========================================================================
    #  步骤2：从临时文件读取所有帧
    # ========================================================================
    print("步骤2: 读取视频帧...")
    cap = cv2.VideoCapture(temp_video)
    if not cap.isOpened():
        print("  无法打开临时视频文件")
        os.remove(temp_video)
        return

    frames = []
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        # 再次 resize 确保尺寸正确（ffmpeg 缩放可能因宽高比产生黑边）
        frame = cv2.resize(frame, (TARGET_W, TARGET_H), interpolation=cv2.INTER_AREA)
        frames.append(frame)
    cap.release()

    # 清理临时视频文件
    os.remove(temp_video)

    if not frames:
        print("  视频没有可用帧"); return

    total_frames = len(frames)
    frame_bytes = TARGET_W * TARGET_H * 2
    blocks, chunk_bytes = solve_blocks(frame_bytes)

    print(f"  源帧率     : {src_fps:.2f}")
    print(f"  强制帧率   : {TARGET_FPS}")
    print(f"  分辨率     : {TARGET_W} x {TARGET_H}")
    print(f"  帧数       : {total_frames}（强制 {TARGET_FPS}fps 后）")
    print(f"  视频帧大小 : {frame_bytes} 字节")
    print(f"  分块       : 每帧 {blocks} 块，每块 {chunk_bytes} 字节 ({chunk_bytes // 512} 个扇区)")

    # ========================================================================
    #  步骤3：提取音频
    # ========================================================================
    print("步骤3: 提取音频...")
    audio = extract_audio(video_path)

    # ★ 计算每帧音频字节数 (必须 4 字节对齐)
    audio_bytes_per_frame = (AUDIO_RATE * AUDIO_CH * 2) // TARGET_FPS
    if audio_bytes_per_frame % AUDIO_DMA_ALIGN != 0:
        raise RuntimeError(
            f"每帧音频 {audio_bytes_per_frame} 字节无法 {AUDIO_DMA_ALIGN} 字节对齐！\n"
            f"原因：{AUDIO_RATE}Hz / {TARGET_FPS}fps 除不尽。\n"
            f"解决：请将 TARGET_FPS 改为 25 或 30 (44100 的因数)。"
        )

    # ========================================================================
    #  步骤4：音视频时长对齐 —— 时长不一致时跳帧（裁掉较长一方）
    # ========================================================================
    video_duration = total_frames / TARGET_FPS  # 视频时长（秒）

    if audio:
        audio_duration = len(audio) / (AUDIO_RATE * AUDIO_CH * 2)  # 音频时长（秒）
    else:
        audio_duration = 0

    print(f"\n  视频时长 : {video_duration:.2f} 秒 ({total_frames} 帧 @ {TARGET_FPS}fps)")
    if audio:
        print(f"  音频时长 : {audio_duration:.2f} 秒 ({len(audio)} 字节)")

    # 【核心对齐逻辑】哪个长就裁哪个，不补静音
    if audio and audio_duration > video_duration:
        # 音频比视频长 -> 截断音频到视频时长
        keep_audio_bytes = int(video_duration * AUDIO_RATE * AUDIO_CH * 2)
        # 确保 4 字节对齐
        keep_audio_bytes = (keep_audio_bytes // AUDIO_DMA_ALIGN) * AUDIO_DMA_ALIGN
        audio = audio[:keep_audio_bytes]
        print(f"  [对齐] 音频比视频长 {audio_duration - video_duration:.2f}秒，截断音频到 {len(audio)} 字节")
    elif audio and audio_duration < video_duration:
        # 视频比音频长 -> 丢弃尾部视频帧，只保留音频能覆盖的帧数
        keep_frames = int(audio_duration * TARGET_FPS)
        if keep_frames < 1:
            keep_frames = 1
        # 多出的音频数据不需要管（我们只取 video_duration 对应的帧）
        # 但需要确保 frames 列表只保留 keep_frames 个
        frames = frames[:keep_frames]
        total_frames = keep_frames
        video_duration = total_frames / TARGET_FPS
        print(f"  [对齐] 视频比音频长 {video_duration - audio_duration:.2f}秒，"
              f"丢弃尾部帧到 {total_frames} 帧（音频覆盖范围）")

    # 重新计算需要的音频总量
    need_audio_bytes = audio_bytes_per_frame * total_frames
    if audio:
        if len(audio) < need_audio_bytes:
            # 音频不够（理论上对齐后不应出现，但防御性处理）
            audio = audio + b"\x00" * (need_audio_bytes - len(audio))
            print(f"  [警告] 音频不足，补静音到 {need_audio_bytes} 字节")
        elif len(audio) > need_audio_bytes:
            # 音频多了（对齐后也不应出现，但防御性处理）
            audio = audio[:need_audio_bytes]

    print(f"\n  最终帧数 : {total_frames}")
    print(f"  音频帧   : {audio_bytes_per_frame} 字节/帧 (交错存储)")
    if audio:
        print(f"  音频总   : {len(audio)} 字节")

    # ========================================================================
    #  步骤5：写入 .vid 文件
    # ========================================================================
    out_path = os.path.splitext(video_path)[0] + ".vid"

    # ★ 字段顺序 / 类型必须和 video_file.h 的 VideoHeader 完全一致
    # 偏移24的 audio_offset 废弃置0；偏移28的 audio_bytes 改为 audio_bytes_per_frame
    hdr = struct.pack('<IHHHHIIIIIIBBH',
                      VIDEO_MAGIC,             # 0: Magic
                      TARGET_W, TARGET_H,      # 4, 6: W, H
                      TARGET_FPS,              # 8: FPS
                      blocks,                  # 10: blocks
                      chunk_bytes,             # 12: chunk_bytes
                      frame_bytes,             # 16: frame_bytes
                      total_frames,            # 20: total_frames
                      0,                       # 24: audio_offset (废弃，交错格式不需要)
                      audio_bytes_per_frame,   # 28: ★ 每帧音频字节数
                      AUDIO_RATE,              # 32: sample_rate
                      AUDIO_CH,                # 36: channels
                      AUDIO_BITS,              # 37: bits
                      0)                       # 38: reserved
    assert len(hdr) == 40, f"文件头长度错了: {len(hdr)}"

    with open(out_path, "wb") as f:
        f.write(hdr)

        # ★ 交错写入：[视频帧] [音频帧] [视频帧] [音频帧] ...
        for i, frame in enumerate(frames):
            # 1. 写入视频帧
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            r = rgb[:, :, 0].astype(np.uint16)
            g = rgb[:, :, 1].astype(np.uint16)
            b = rgb[:, :, 2].astype(np.uint16)
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            f.write(rgb565.tobytes())

            # 2. 写入对应的音频帧
            if audio and audio_bytes_per_frame > 0:
                a_start = i * audio_bytes_per_frame
                a_end   = a_start + audio_bytes_per_frame
                f.write(audio[a_start:a_end])

            if (i + 1) % 20 == 0 or i + 1 == total_frames:
                print(f"\r  写入帧: {i + 1}/{total_frames}", end="")

    print()
    size = os.path.getsize(out_path)
    expected_size = 40 + total_frames * (frame_bytes + audio_bytes_per_frame)
    print(f"完成: {out_path}")
    print(f"  文件大小 {size} 字节 (应为 {expected_size})")
    print(f"  校验: 头40B + ({frame_bytes}B视频 + {audio_bytes_per_frame}B音频) x {total_frames}帧 = {expected_size}")

# ============================================================================
if __name__ == "__main__":
    p = input("请输入视频路径：\n").strip().strip('"')
    if not os.path.exists(p):
        print("文件不存在")
    else:
        # ★ 输入目标分辨率
        res_input = input(
            "请输入目标分辨率（宽 高，例如 320 480，直接回车用默认 320 480）：\n"
        ).strip()

        if res_input:
            try:
                parts = res_input.replace(",", " ").split()
                if len(parts) != 2:
                    raise ValueError("需要两个数字")
                w_in, h_in = int(parts[0]), int(parts[1])
                TARGET_W, TARGET_H = normalize_resolution(w_in, h_in)
                frame_bytes_tmp = TARGET_W * TARGET_H * 2
                print(f"已调整分辨率：{TARGET_W}x{TARGET_H}")
                print(f"  一帧字节数：{frame_bytes_tmp} 字节")
                print(f"  扇区数：{frame_bytes_tmp // 512}")
                print(f"  512 倍数校验：{frame_bytes_tmp % 512 == 0}")
            except Exception as e:
                print(f"分辨率输入无效（{e}），使用默认 {TARGET_W}x{TARGET_H}")
        else:
            print(f"使用默认分辨率：{TARGET_W}x{TARGET_H}")

        convert(p)

    input("按回车键退出...")