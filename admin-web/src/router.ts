import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/login', component: () => import('./views/LoginView.vue') },
    {
      path: '/',
      component: () => import('./views/LayoutView.vue'),
      meta: { auth: true },
      children: [
        { path: '', redirect: '/dashboard' },
        { path: 'dashboard', component: () => import('./views/DashboardView.vue') },
        { path: 'players', component: () => import('./views/PlayersView.vue') },
        { path: 'ledgers', component: () => import('./views/LedgersView.vue') },
        { path: 'rounds', component: () => import('./views/RoundsView.vue') },
        { path: 'templates', component: () => import('./views/TemplatesView.vue') },
        { path: 'pay/products', component: () => import('./views/ProductsView.vue') },
        { path: 'pay/orders', component: () => import('./views/OrdersView.vue') },
        { path: 'announce', component: () => import('./views/AnnounceView.vue') },
        { path: 'mail', component: () => import('./views/MailView.vue') },
        { path: 'ops', component: () => import('./views/OpsView.vue') },
        { path: 'audit', component: () => import('./views/AuditView.vue') },
        { path: 'activities', component: () => import('./views/ActivitiesView.vue') },
        { path: 'reports', component: () => import('./views/ReportsView.vue') },
      ],
    },
  ],
})

router.beforeEach((to) => {
  if (!to.meta.auth && to.path !== '/' && !to.matched.some((r) => r.meta.auth)) return true
  if (to.path === '/login') return true
  if (to.matched.some((r) => r.meta.auth) && !localStorage.getItem('admin_token')) {
    return '/login'
  }
  return true
})

export default router
