import { defineConfig } from 'vitepress'
import { MermaidMarkdown,MermaidPlugin } from "vitepress-plugin-mermaid";
import mermaid from "mermaid";


// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: "MQEngine",
  description: "Document for MQEngine",
    ignoreDeadLinks: true,
    base: '/MQEngine/',
    markdown: {
      config(md) {
          md.use(MermaidMarkdown);
      }
    },
    vite : {
      plugins: [MermaidPlugin()],
      optimizeDeps: {
          include: 'mermaid',
      },
      ssr: {
          noExternal: ['mermaid'],
      },
    },
    themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    nav: [
      { text: '主页', link: '/' },
      { text: '文档', link: '/document/index.md'},
    ],

    sidebar: [
      {
        text: '文档',
        link: '/document/index.md',
        items: [
            {
                text: '关于',
                link: '/document/about/index.md',
                items: []
            },
            {
                text: '入门',
                link: '/document/getting_started/index.md',
                items: [
                    {

                    }]
            },
            {
                text: '手册',
                link: '/document/tutorials/index.md',
                items: [
                    {

                    }]
            },
            {
                text: '贡献',
                link: '/document/contributing/index.md',
                items: [
                    {
                        text: '引擎开发',
                        link: '/document/contributing/development/index.md',
                        items: [
                            {
                                text: '引擎架构',
                                link: '/document/contributing/development/engine-architecture.md',
                                items: [

                                ]
                            }
                        ]
                    }
                ]
            }
        ]
      }
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/MaaBlock/MQEngine' }
    ]
  }
})
