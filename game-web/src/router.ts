import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/login' },
    {
      path: '/login',
      name: 'login',
      component: () => import('./views/LoginView.vue'),
    },
    {
      path: '/lobby',
      name: 'lobby',
      component: () => import('./views/LobbyView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/table',
      name: 'table',
      component: () => import('./views/TableView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/wallet',
      name: 'wallet',
      component: () => import('./views/WalletView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/activity',
      name: 'activity',
      component: () => import('./views/ActivityView.vue'),
      meta: { requiresAuth: true },
    },
  ],
})

router.beforeEach((to) => {
  if (!to.meta.requiresAuth) return true
  const token = sessionStorage.getItem('pandora_token')
  if (!token) return { path: '/login' }
  return true
})

export default router

