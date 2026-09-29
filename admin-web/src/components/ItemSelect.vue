<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import { api } from '../api'

export type ItemDefOption = {
  id: number
  name: string
  kind: string
  enabled?: boolean
}

const props = withDefaults(
  defineProps<{
    modelValue: number | undefined | null
    /** 是否包含未上架道具 */
    includeDisabled?: boolean
    /** 允许空选（筛选场景） */
    clearable?: boolean
    /** 空选占位 */
    placeholder?: string
    /** 下拉宽度，默认 100% */
    width?: string
  }>(),
  {
    includeDisabled: false,
    clearable: false,
    placeholder: '选择道具',
    width: '100%',
  },
)

const emit = defineEmits<{
  'update:modelValue': [value: number | undefined]
}>()

const options = ref<ItemDefOption[]>([])
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    const r = await api.items()
    if (r.code === 0) {
      const list = (r.data.items || []) as ItemDefOption[]
      options.value = props.includeDisabled ? list : list.filter((x) => x.enabled !== false)
    }
  } finally {
    loading.value = false
  }
}

watch(
  () => props.includeDisabled,
  () => load(),
)

onMounted(load)

function onChange(v: number | undefined | null | string) {
  if (v == null || v === '') {
    emit('update:modelValue', props.clearable ? undefined : 0)
    return
  }
  emit('update:modelValue', Number(v))
}

defineExpose({ reload: load, options })
</script>

<template>
  <el-select
    :model-value="modelValue == null || modelValue <= 0 ? undefined : modelValue"
    filterable
    :clearable="clearable"
    :loading="loading"
    :placeholder="placeholder"
    :style="{ width }"
    @update:model-value="onChange"
  >
    <el-option
      v-for="d in options"
      :key="d.id"
      :value="d.id"
      :label="`${d.id} ${d.name} (${d.kind})`"
    />
  </el-select>
</template>
