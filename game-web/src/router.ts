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
      path: '/hzmj-table',
      name: 'hzmj-table',
      component: () => import('./views/HzmjTableView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/hzmj-lab',
      name: 'hzmj-lab',
      component: () => import('./views/HzmjLabView.vue'),
    },
    {
      path: '/ddz-lab',
      name: 'ddz-lab',
      component: () => import('./views/DdzLabView.vue'),
    },
    {
      path: '/phz-lab',
      name: 'phz-lab',
      component: () => import('./views/PhzLabView.vue'),
    },
    {
      path: '/phz-table',
      name: 'phz-table',
      component: () => import('./views/PhzTableView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/fish',
      name: 'fish',
      component: () => import('./views/FishTableView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/fish-lab',
      name: 'fish-lab',
      component: () => import('./views/FishLabView.vue'),
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
    {
      path: '/record',
      name: 'record',
      component: () => import('./views/RecordView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/friends',
      name: 'friends',
      component: () => import('./views/FriendsView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/mail',
      name: 'mail',
      component: () => import('./views/MailView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/rank',
      name: 'rank',
      component: () => import('./views/RankView.vue'),
      meta: { requiresAuth: true },
    },
    {
      path: '/bag',
      name: 'bag',
      component: () => import('./views/BagView.vue'),
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

