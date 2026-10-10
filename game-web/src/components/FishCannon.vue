<script setup lang="ts">
const props = defineProps<{
  leftPct: number
  bottomPct: number
  angle: number
  mult: number
  nickname: string
  me?: boolean
  firing?: boolean
  empty?: boolean
  /** Top-row seat: base on outer edge, barrel toward pond center. */
  top?: boolean
}>()

/** CSS rotate: 0 = toward +Y (screen up). Top seats add 180 so 0 faces into pond. */
function barrelRotate() {
  return props.top ? props.angle + 180 : props.angle
}
</script>

<template>
  <div
    class="cannon"
    :class="{ me, firing, empty, top }"
    :style="
      top
        ? { left: leftPct + '%', top: '0' }
        : { left: leftPct + '%', bottom: bottomPct + '%' }
    "
  >
    <div class="base">
      <span class="ring" />
    </div>
    <div class="barrel-wrap" :style="{ transform: `rotate(${barrelRotate()}deg)` }">
      <div class="barrel">
        <span class="glow" />
      </div>
      <div class="muzzle" />
    </div>
    <div class="plate">
      <strong>{{ empty ? '空座' : nickname || '玩家' }}</strong>
      <em v-if="!empty">{{ mult }}×</em>
    </div>
  </div>
</template>

<style scoped>
.cannon {
  --base-size: 48px;
  position: absolute;
  width: 88px;
  height: 96px;
  transform: translate(-50%, 45%);
  z-index: 5;
  pointer-events: none;
}
.cannon.top {
  /* Flush with pond top edge; grow downward into the pond. */
  transform: translate(-50%, 0);
  height: 100px;
  margin: 0;
}
.cannon.empty { opacity: 0.4; }
.cannon.me .plate {
  border-color: rgba(240, 209, 106, 0.7);
  color: #f0d16a;
}
.cannon.firing .barrel {
  filter: brightness(1.35);
  box-shadow: 0 0 16px rgba(255, 220, 120, 0.85);
}
.cannon.firing .muzzle {
  background: #fff8c8;
  box-shadow: 0 0 12px #ffe08a;
}

/* ---- Base (gold pedestal) ---- */
.base {
  position: absolute;
  left: 50%;
  bottom: 6px;
  width: var(--base-size);
  height: 32px;
  margin-left: calc(var(--base-size) / -2);
  border-radius: 50% 50% 42% 42%;
  background: linear-gradient(180deg, #ffe08a 0%, #f0d16a 40%, #b45309 100%);
  box-shadow:
    0 3px 0 #78350f,
    0 8px 14px rgba(0, 0, 0, 0.4),
    inset 0 2px 0 rgba(255, 255, 255, 0.35);
  z-index: 3;
}
.cannon.top .base {
  top: 0;
  bottom: auto;
  border-radius: 0 0 50% 50%;
  box-shadow:
    0 3px 0 #78350f,
    0 8px 14px rgba(0, 0, 0, 0.45),
    inset 0 2px 0 rgba(255, 255, 255, 0.35);
}
.ring {
  position: absolute;
  left: 50%;
  top: 42%;
  width: 16px;
  height: 16px;
  margin: -8px 0 0 -8px;
  border-radius: 50%;
  background: radial-gradient(circle at 35% 30%, #4b5563, #111827 70%);
  box-shadow: inset 0 0 0 2px #fbbf24, 0 1px 2px rgba(0, 0, 0, 0.5);
}

/* ---- Barrel ---- */
.barrel-wrap {
  position: absolute;
  left: 50%;
  bottom: 28px;
  width: 18px;
  height: 54px;
  margin-left: -9px;
  transform-origin: 50% 100%;
  transition: transform 0.08s linear;
  z-index: 2;
}
.cannon.top .barrel-wrap {
  top: 26px;
  bottom: auto;
  transform-origin: 50% 0%;
}
.barrel {
  position: absolute;
  left: 2px;
  right: 2px;
  top: 0;
  bottom: 10px;
  border-radius: 6px 6px 3px 3px;
  background: linear-gradient(90deg, #6b7280, #e5e7eb 45%, #4b5563);
  border: 1px solid rgba(0, 0, 0, 0.35);
}
.cannon.top .barrel {
  top: 10px;
  bottom: 0;
  border-radius: 3px 3px 6px 6px;
}
.glow {
  position: absolute;
  inset: 18% 28%;
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.28);
}
.muzzle {
  position: absolute;
  left: 0;
  right: 0;
  top: -4px;
  height: 8px;
  border-radius: 3px;
  background: #9ca3af;
  border: 1px solid #374151;
}
.cannon.top .muzzle {
  top: auto;
  bottom: -4px;
}

/* ---- Nameplate ---- */
.plate {
  position: absolute;
  left: 50%;
  bottom: -18px;
  transform: translateX(-50%);
  min-width: 64px;
  text-align: center;
  font-size: 10px;
  line-height: 1.2;
  padding: 2px 7px;
  border-radius: 999px;
  background: rgba(0, 0, 0, 0.5);
  border: 1px solid rgba(255, 255, 255, 0.18);
  color: #d7ecf4;
  white-space: nowrap;
  z-index: 4;
}
.cannon.top .plate {
  /* Inside pond, under the barrel tip — never clipped by top edge. */
  top: auto;
  bottom: -4px;
  left: 50%;
  transform: translateX(-50%);
}
.plate strong { font-weight: 700; }
.plate em {
  font-style: normal;
  margin-left: 4px;
  color: #f0d16a;
  font-weight: 700;
}
</style>