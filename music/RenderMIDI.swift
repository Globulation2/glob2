import Foundation
import AVFoundation
import AudioToolbox

struct Event: Decodable { let frame: Int; let status: UInt8; let data1: UInt8; let data2: UInt8 }
struct Job: Decodable { let frames: Int; let events: [Event]; let output: String }
struct Request: Decodable { let soundBank: String; let jobs: [Job] }

enum RenderError: Error { case invalidInput, stalled }
let request = try JSONDecoder().decode(Request.self, from: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])))
let format = AVAudioFormat(standardFormatWithSampleRate: 44100, channels: 2)!
for job in request.jobs {
    guard job.frames > 0 else { throw RenderError.invalidInput }
    let engine = AVAudioEngine()
    var samplers: [UInt8: AVAudioUnitSampler] = [:]
    let channels = Set(job.events.filter { $0.status & 0xf0 == 0x90 }.map { $0.status & 0x0f })
    for channel in channels.sorted() {
        let sampler = AVAudioUnitSampler()
        engine.attach(sampler)
        engine.connect(sampler, to: engine.mainMixerNode, format: format)
        let program = job.events.last { $0.frame == 0 && $0.status == 0xc0 | channel }?.data1 ?? 0
        try sampler.loadSoundBankInstrument(at: URL(fileURLWithPath: request.soundBank), program: program,
            bankMSB: channel == 9 ? UInt8(kAUSampler_DefaultPercussionBankMSB) : UInt8(kAUSampler_DefaultMelodicBankMSB),
            bankLSB: UInt8(kAUSampler_DefaultBankLSB))
        samplers[channel] = sampler
    }
    try engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 512)
    engine.prepare()
    try engine.start()
    let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 512)!
    FileManager.default.createFile(atPath: job.output, contents: nil)
    let output = try FileHandle(forWritingTo: URL(fileURLWithPath: job.output))
    try output.truncate(atOffset: 0)
    var frame = 0
    var stalls = 0
    // Two complete passes prime release tails before capturing one steady-state loop.
    for pass in 0..<3 {
        var eventIndex = 0
        let end = (pass + 1) * job.frames
        while frame < end {
            while eventIndex < job.events.count && pass * job.frames + job.events[eventIndex].frame <= frame {
                let event = job.events[eventIndex]
                if let sampler = samplers[event.status & 0x0f], event.status & 0xf0 != 0xc0 {
                    sampler.sendMIDIEvent(event.status, data1: event.data1, data2: event.data2)
                }
                eventIndex += 1
            }
            let next = eventIndex < job.events.count ? pass * job.frames + job.events[eventIndex].frame : end
            let count = AVAudioFrameCount(min(512, min(next, end) - frame))
            guard count > 0 else { throw RenderError.invalidInput }
            let status = try engine.renderOffline(count, to: buffer)
            if status == .success {
                guard buffer.frameLength == count else { throw RenderError.stalled }
                if pass == 2 {
                    let channels = buffer.floatChannelData!
                    var samples = [Float]()
                    samples.reserveCapacity(Int(buffer.frameLength) * 2)
                    for i in 0..<Int(buffer.frameLength) {
                        samples.append(channels[0][i]); samples.append(channels[1][i])
                    }
                    try samples.withUnsafeBytes { try output.write(contentsOf: Data($0)) }
                }
                frame += Int(buffer.frameLength)
                stalls = 0
            } else {
                stalls += 1
                if stalls > 100 { throw RenderError.stalled }
            }
        }
        // Deliver note-offs at the exact loop boundary before replaying tick zero.
        while eventIndex < job.events.count {
            let event = job.events[eventIndex]
            if let sampler = samplers[event.status & 0x0f], event.status & 0xf0 != 0xc0 {
                sampler.sendMIDIEvent(event.status, data1: event.data1, data2: event.data2)
            }
            eventIndex += 1
        }
    }
    try output.close()
    engine.stop()
    print("Rendered \(job.output)")
}
