/**
 * 食用方法：
 * 设置DEEPSEEK_API_KEY环境变量
 * 添加一个实体，将aiJokeDemo_aiJokeTicker拖入当ticker类型脚本
 */

const OpenAI = require('openai');

var aiJokeDemo_client = null;
var aiJokeDemo_isInitializing = false;
var aiJokeDemo_lastJokeTime = 0;
const aiJokeDemo_JOKE_INTERVAL = 10000; 

async function aiJokeDemo_initializeAI() {
  aiJokeDemo_isInitializing = true;
  console.log('正在初始化 AI 客户端...');

  const apiKey = process.env.DEEPSEEK_API_KEY;
  if (!apiKey) {
    console.error('错误：DEEPSEEK_API_KEY 环境变量未设置。');
    aiJokeDemo_isInitializing = false;
    return;
  }

  try {
    aiJokeDemo_client = new OpenAI({
      apiKey: apiKey,
      baseURL: 'https://api.deepseek.com/v1',
    });
    console.log('AI 客户端已成功初始化。');
  } catch (error) {
    console.error('初始化 AI 客户端时出错:', error.message);
  } finally {
    aiJokeDemo_isInitializing = false;
  }
}

async function aiJokeDemo_getProgrammerJoke() {
  if (!aiJokeDemo_client) {
    console.error('错误：AI 客户端不可用。');
    return;
  }

  try {
    console.log('正在向 DeepSeek AI 请求一个程序员笑话...');
    
    const response = await aiJokeDemo_client.chat.completions.create({
      model: 'deepseek-chat',
      messages: [
        { role: 'user', content: '请给我讲一个关于程序员的简短笑话。' },
      ],
      stream: false,
    });

    const reply = response.choices[0].message.content;
    console.log('AI 笑话:', reply);

  } catch (error) {
    console.error('调用 DeepSeek API 时出错:', error.message);
  }
}

function aiJokeDemo_aiJokeTicker() {
  if (!aiJokeDemo_client && !aiJokeDemo_isInitializing) {
    aiJokeDemo_initializeAI();
  }

  if (!aiJokeDemo_client) {
    return;
  }

  const currentTime = Date.now();

  if (aiJokeDemo_lastJokeTime === 0) {
    aiJokeDemo_getProgrammerJoke();
    aiJokeDemo_lastJokeTime = currentTime;
    return;
  }

  if (currentTime - aiJokeDemo_lastJokeTime >= aiJokeDemo_JOKE_INTERVAL) {
    aiJokeDemo_getProgrammerJoke();
    aiJokeDemo_lastJokeTime = currentTime;
  }
}

