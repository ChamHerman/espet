# ESPET 麦克风、扬声器、TTS 与 STT 状态清单

本文档记录了项目中关于**麦克风（INMP441）、扬声器（MAX98357A）、语音识别（STT / Whisper）与语音合成（TTS / edge-tts）**的最新配置与代码状态。

---

## 1. 核心状态概览

| 模块 / 功能 | 当前状态 | 说明 |
|---|---|---|
| **"Hi ESP" 离线免提唤醒** | ✅ **已恢复启用** | `voice_client_start()` 在 `app_main` 中启动，Core 0 持续运行 WakeNet WN9 唤醒词检测 |
| **唤醒应答音 ("Yes?")** | ✅ **已启用** | 唤醒后扬声器立即播放预缓存的 "Yes?" WAV 音频（或 880Hz 提示音） |
| **纯本地语音通道（不发 Telegram）**| ✅ **已完全隔离** | 语音对话经 Whisper 转写、LLM 思考后，**仅通过 MAX98357A 扬声器播报**，绝不转发至 Telegram |
| **Telegram 消息 TTS 朗读** | ✅ **已保留** | 手机端向 Telegram 发送文字时，设备端仍可通过扬声器朗读回复 |
| **单按键控制（GPIO 41）** | ✅ **相机专属** | 短按拍照，长按录像，硬件 PTT 彻底移除 |
| **底层 I2S 全双工驱动** | ✅ **正常运行** | BCLK=38, WS=39, DIN=40, DOUT=21，开机由 `tool_registry_init()` 初始化 |
| **后端 STT / TTS 服务** | ✅ **正常运行** | `espet-backend` 上的 `/api/voice/transcribe` 和 `/api/voice/tts` 正常挂载 |

---

## 2. 唤醒与麦克风测试操作指引

### 测试 A：快速验证麦克风收音硬件 (串口命令)
在串口终端输入以下命令：
```text
tool_exec audio_record_test "{}"
```
* **观察点**:
  - 对着 INMP441 麦克风说话，观察返回的 JSON。
  - `rms` > 300 且 `verdict` 为 `"audio_detected"` 说明硬件录音与麦克风接线完全正常。

### 测试 B：唤醒词检测与扬声器回 "Yes?" 测试
1. 烧录固件并打开 Monitor：
2. 观察开机日志：
   - 看到 `WakeNet "wn9_hiesp" ready (wake word: "hiesp")`
   - 看到 `ack WAV cached`（或 `falling back to beep`）
   - 看到 `wake listener running — say "Hi ESP"`
   - 观察每 5 秒的监听日志：`[listen] 5s: frames=... peak_rms=...`（确认麦克风帧正在输入）。
3. 距离麦克风约 30~50cm 清晰说出：
   > **"Hi ESP"**
4. **预期反馈**:
   - 串口立即打印：`*** WAKE WORD DETECTED ***`
   - OLED 屏幕切换为：`Listening...`
   - MAX98357A 扬声器立即发出声音：**"Yes?"**
   - 随后进入语音录音与对话处理流程，回复将直接从扬声器输出，**Telegram 聊天中不会出现任何打扰内容**。
