import EventEmitter from "events";
import fs from "fs";
import os from "os";
import path from "path";
import { spawn } from "child_process";
import { randomUUID } from "crypto";

import { config } from "./config.js";
import { appendJsonl } from "./logStore.js";
import { FireCommand, TimelineEvent, TimelinePayload, timelineSchema } from "./types.js";

interface SchedulerArtifacts {
  schedulerJson: unknown;
  leadSeconds: number;
  baseTimeIso: string;
}

interface PreparedTimeline {
  events: TimelinePayload["events"];
  leadSeconds: number;
}

const MIN_LEAD_SECONDS = 3;
const AUTO_SHIFT_MAX_DURATION_MS = 15 * 60 * 1000; // only auto-shift short test timelines
const AUTO_SHIFT_PAST_THRESHOLD_MS = 5 * 1000;
const AUTO_SHIFT_FUTURE_THRESHOLD_MS = 2 * 60 * 60 * 1000;

function parseIsoToMillis(value: string): number {
  const millis = Date.parse(value);
  if (Number.isNaN(millis)) {
    throw new Error(`Invalid ISO-8601 timestamp: ${value}`);
  }
  return millis;
}

function ensureUniformLead(events: TimelinePayload["events"]): number {
  if (events.length === 0) {
    throw new Error("Timeline must contain at least one event");
  }
  const firstLeadSeconds = events[0].lead_time_ms / 1000;
  if (firstLeadSeconds < MIN_LEAD_SECONDS) {
    throw new Error(`lead_time_ms must be >= ${MIN_LEAD_SECONDS * 1000}`);
  }
  for (const event of events) {
    const leadSeconds = event.lead_time_ms / 1000;
    if (Math.abs(leadSeconds - firstLeadSeconds) > 1e-3) {
      throw new Error("Mixed lead_time_ms values are not supported yet");
    }
  }
  return firstLeadSeconds;
}

function buildSchedulerArtifacts(timeline: TimelinePayload, prepared?: PreparedTimeline): SchedulerArtifacts {
  if (timeline.events.length === 0) {
    throw new Error("Timeline must contain at least one event");
  }

  const sortedEvents =
    prepared?.events ??
    [...timeline.events].sort((a, b) => {
      return parseIsoToMillis(a.time_utc) - parseIsoToMillis(b.time_utc);
    });

  const leadSeconds = prepared?.leadSeconds ?? ensureUniformLead(sortedEvents);
  const firstEventMillis = parseIsoToMillis(sortedEvents[0].time_utc);

  const schedulerEvents = sortedEvents.map((event) => {
    const eventMillis = parseIsoToMillis(event.time_utc);
    const offsetSeconds = Number(((eventMillis - firstEventMillis) / 1000).toFixed(3));
    const gain = typeof event.gain === "number" ? event.gain : 1.0;
    return {
      offset: offsetSeconds,
      address: "/acoustics/play",
      targets: event.targets,
      args: [event.preset, 0, gain, 0]
    };
  });

  const schedulerJson = {
    version: "1.2",
    default_lead_time: leadSeconds,
    metadata: {
      source_timeline_id: timeline.timeline_id
    },
    events: schedulerEvents
  };

  const baseTimeMillis = firstEventMillis - leadSeconds * 1000;
  const baseTimeIso = new Date(baseTimeMillis).toISOString();

  return {
    schedulerJson,
    leadSeconds,
    baseTimeIso
  };
}

export interface TimelineDispatchResult {
  requestId: string;
  events: TimelineEvent[];
}

export class TimelineService extends EventEmitter {
  private events: TimelineEvent[] = [];

  getEvents(): TimelineEvent[] {
    return [...this.events];
  }

  async preview(payload: unknown): Promise<TimelineEvent[]> {
    const parsed = timelineSchema.parse(payload);
    const { aligned } = alignTimeline(parsed);
    return this.flattenTimeline(aligned, randomUUID());
  }

  async dispatch(payload: unknown, requestId = randomUUID()): Promise<TimelineDispatchResult> {
    const parsed = timelineSchema.parse(payload);
    const { aligned, prepared } = alignTimeline(parsed);
    const events = this.flattenTimeline(aligned, requestId);
    this.appendEvents(events);
    this.emitUpdate();
    await appendJsonl(config.auditLogPath, { type: "timeline_dispatch", requestId, timeline_id: aligned.timeline_id });
    await this.invokeScheduler(aligned, requestId, prepared);
    return { requestId, events };
  }

  async fire(command: FireCommand): Promise<TimelineDispatchResult> {
    const timeUtc = new Date(Date.now() + command.leadTimeMs).toISOString();
    const payload: TimelinePayload = {
      timeline_id: `single-fire-${command.requestId}`,
      events: [
        {
          time_utc: timeUtc,
          targets: command.targets,
          preset: command.presetId,
          lead_time_ms: command.leadTimeMs,
          gain: command.gainDb
        }
      ]
    };
    return this.dispatch(payload, command.requestId);
  }

  private appendEvents(events: TimelineEvent[]): void {
    this.events.push(...events);
    const MAX = 500;
    if (this.events.length > MAX) {
      this.events.splice(0, this.events.length - MAX);
    }
  }

  private flattenTimeline(payload: TimelinePayload, requestId: string): TimelineEvent[] {
    return payload.events.map((event, index) => ({
      id: `${requestId}-${index}`,
      requestId,
      preset: event.preset,
      targets: event.targets,
      scheduledTimeUtc: event.time_utc,
      status: "scheduled" as TimelineEvent["status"]
    }));
  }

  private async invokeScheduler(
    timeline: TimelinePayload,
    requestId: string,
    prepared?: PreparedTimeline
  ): Promise<void> {
    if (!fs.existsSync(config.schedulerBinary)) {
      console.warn(`[TimelineService] Scheduler binary not found: ${config.schedulerBinary}`);
      this.markEvents(requestId, "failed");
      this.emitUpdate();
      return;
    }
    let artifacts: SchedulerArtifacts;
    try {
      artifacts = buildSchedulerArtifacts(timeline, prepared);
    } catch (error) {
      console.error("[TimelineService] timeline conversion failed:", error);
      this.markEvents(requestId, "failed");
      this.emitUpdate();
      return;
    }
    const tempDir = await fs.promises.mkdtemp(path.join(os.tmpdir(), "timeline-"));
    const timelinePath = path.join(tempDir, `${timeline.timeline_id || requestId}.json`);
    await fs.promises.writeFile(timelinePath, JSON.stringify(artifacts.schedulerJson, null, 2), "utf-8");
    const args = [
      timelinePath,
      "--host",
      config.defaultSend.host,
      "--port",
      config.defaultSend.port.toString(),
      "--lead-time",
      artifacts.leadSeconds.toString(),
      "--base-time",
      artifacts.baseTimeIso,
      "--osc-config",
      config.oscConfigPath
    ];
    if (config.dryRun) {
      args.push("--dry-run");
    }
    await appendJsonl(config.auditLogPath, { type: "scheduler_exec", requestId, args });
    await this.runProcess(config.schedulerBinary, args)
      .then(() => {
        this.markEvents(requestId, "sent");
      })
      .catch((error) => {
        console.error("[TimelineService] scheduler failed:", error);
        this.markEvents(requestId, "failed");
      })
      .finally(() => this.emitUpdate());
  }

  private markEvents(requestId: string, status: TimelineEvent["status"]): void {
    this.events = this.events.map((event) => {
      if (event.requestId === requestId) {
        return { ...event, status: status === "sent" ? "sent" : "failed" };
      }
      return event;
    });
  }

  private emitUpdate(): void {
    this.emit("update", this.getEvents());
  }

  private runProcess(cmd: string, args: string[]): Promise<void> {
    return new Promise((resolve, reject) => {
      const child = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"] });
      child.stdout?.on("data", (chunk) => process.stdout.write(`[scheduler] ${chunk}`));
      child.stderr?.on("data", (chunk) => process.stderr.write(`[scheduler] ${chunk}`));
      child.on("error", reject);
      child.on("close", (code) => {
        if (code === 0) {
          resolve();
        } else {
          reject(new Error(`scheduler exited with code ${code}`));
        }
      });
    });
  }
}

export function createTimelineService(): TimelineService {
  return new TimelineService();
}

function alignTimeline(
  timeline: TimelinePayload
): { aligned: TimelinePayload; prepared: PreparedTimeline } {
  if (timeline.events.length === 0) {
    throw new Error("Timeline must contain at least one event");
  }

  const sortedEvents = [...timeline.events].sort((a, b) => {
    return parseIsoToMillis(a.time_utc) - parseIsoToMillis(b.time_utc);
  });
  const leadSeconds = ensureUniformLead(sortedEvents);
  const { events: alignedEvents, shiftMillis } = maybeAutoShiftEvents(sortedEvents, leadSeconds);
  if (shiftMillis !== 0) {
    const shiftedSeconds = (shiftMillis / 1000).toFixed(3);
    console.log(
      `[TimelineService] auto-shifted timeline ${timeline.timeline_id} by ${shiftedSeconds}s to align with current time`
    );
  }

  const aligned: TimelinePayload = {
    ...timeline,
    events: alignedEvents
  };

  return {
    aligned,
    prepared: {
      events: alignedEvents,
      leadSeconds
    }
  };
}

function maybeAutoShiftEvents(
  events: TimelinePayload["events"],
  leadSeconds: number
): { events: TimelinePayload["events"]; shiftMillis: number } {
  if (events.length === 0) {
    return { events, shiftMillis: 0 };
  }

  const firstEventMillis = parseIsoToMillis(events[0].time_utc);
  const lastEventMillis = parseIsoToMillis(events[events.length - 1].time_utc);
  const durationMs = lastEventMillis - firstEventMillis;
  if (durationMs > AUTO_SHIFT_MAX_DURATION_MS) {
    return { events, shiftMillis: 0 };
  }

  const desiredFirstMillis = Date.now() + leadSeconds * 1000;
  const earliestAllowed = desiredFirstMillis - AUTO_SHIFT_PAST_THRESHOLD_MS;
  const latestAllowed = desiredFirstMillis + AUTO_SHIFT_FUTURE_THRESHOLD_MS;

  if (firstEventMillis >= earliestAllowed && firstEventMillis <= latestAllowed) {
    return { events, shiftMillis: 0 };
  }

  const shiftMillis = desiredFirstMillis - firstEventMillis;
  const shiftedEvents = events.map((event) => {
    const shiftedMillis = parseIsoToMillis(event.time_utc) + shiftMillis;
    return {
      ...event,
      time_utc: new Date(shiftedMillis).toISOString()
    };
  });
  return { events: shiftedEvents, shiftMillis };
}
