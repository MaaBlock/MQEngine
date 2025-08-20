import { defineConfig } from 'vitepress'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: "MQEngine",
  description: "Document for MQEngine",
    base: '/MQEngine/',
    themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    nav: [
      { text: '主页', link: '/' },
      { text: '教程', link: '/markdown-examples'}
    ],

    sidebar: [
      {
        text: '教程',
        items: [
          { text: 'Markdown Examples', link: '/markdown-examples' },
        ]
      }
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/MaaBlock/MQEngine' }
    ]
  }
})
