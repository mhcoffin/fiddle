import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import ToolPane from './lib/ToolPane.svelte'

// Tools subscribe only to their own state, never a duplicate mixer/debug feed.
const toolId = new URLSearchParams(window.location.search).get('mode')
const pane = ['print', 'chairs', 'bus-manager', 'audio-performance', 'technique-catalog'].includes(toolId) ? ToolPane : App
const app = mount(pane, {
  target: document.getElementById('app'),
  props: pane === ToolPane ? { toolId } : {},
})

export default app
