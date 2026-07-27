/**
 * AudioEncoder - Handles encoding audio buffers to various formats
 */
export class AudioEncoder {
    /**
     * Create a new AudioEncoder instance
     */
    constructor() {
        // No initialization needed
    }
    
    /**
     * Encode audio buffer to WAV format with 24-bit samples
     * @param {AudioBuffer} audioBuffer - The audio buffer to encode
     * @returns {Blob} - WAV file as a Blob
     */
    encodeWAV(audioBuffer) {
        // Helper function to write string data into DataView
        const writeString = (view, offset, string) => {
            for (let i = 0; i < string.length; i++) {
                view.setUint8(offset + i, string.charCodeAt(i));
            }
        };

        const format = 1; // PCM
        const numChannels = audioBuffer.numberOfChannels;
        const sampleRate = audioBuffer.sampleRate;
        const bitsPerSample = 24; // 24-bit
        const bytesPerSample = bitsPerSample / 8; // 3 bytes per sample
        const blockAlign = numChannels * bytesPerSample;
        const byteRate = sampleRate * blockAlign;
        const samples = audioBuffer.length;
        const dataSize = samples * blockAlign;
        const fileSize = 36 + dataSize;

        // Create buffer for WAV file
        const buffer = new ArrayBuffer(44 + dataSize);
        const view = new DataView(buffer);
        let offset = 0;

        // RIFF chunk descriptor
        writeString(view, offset, 'RIFF'); offset += 4;
        view.setUint32(offset, fileSize, true); offset += 4;
        writeString(view, offset, 'WAVE'); offset += 4;

        // fmt sub-chunk
        writeString(view, offset, 'fmt '); offset += 4;
        view.setUint32(offset, 16, true); offset += 4; // Subchunk1Size for PCM
        view.setUint16(offset, format, true); offset += 2;
        view.setUint16(offset, numChannels, true); offset += 2;
        view.setUint32(offset, sampleRate, true); offset += 4;
        view.setUint32(offset, byteRate, true); offset += 4;
        view.setUint16(offset, blockAlign, true); offset += 2;
        view.setUint16(offset, bitsPerSample, true); offset += 2;

        // data sub-chunk
        writeString(view, offset, 'data'); offset += 4;
        view.setUint32(offset, dataSize, true); offset += 4;

        // Write audio samples to buffer as 24-bit little endian
        const channels = [];
        for (let i = 0; i < numChannels; i++) {
            channels.push(audioBuffer.getChannelData(i));
        }

        let index = 0;
        for (let i = 0; i < samples; i++) {
            for (let ch = 0; ch < numChannels; ch++) {
                let sample = channels[ch][i];
                // Clamp the sample value between -1 and 1
                sample = Math.max(-1, Math.min(1, sample));
                // Scale sample to 24-bit range
                let intSample;
                if (sample < 0) {
                    intSample = Math.round(sample * 0x800000);
                } else {
                    intSample = Math.round(sample * 0x7FFFFF);
                }
                // Convert to unsigned 24-bit integer (two's complement)
                let intSample24 = intSample & 0xFFFFFF;
                view.setUint8(offset + index, intSample24 & 0xFF); // Least significant byte
                view.setUint8(offset + index + 1, (intSample24 >> 8) & 0xFF);
                view.setUint8(offset + index + 2, (intSample24 >> 16) & 0xFF);
                index += 3;
            }
        }

        return new Blob([buffer], { type: 'audio/wav' });
    }
}