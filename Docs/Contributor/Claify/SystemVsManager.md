# System vs Manager
## System
ECS的System，管理一帧内的数据，比如，某一个数据，当前帧使用，然后释放也可以直接释放
## Magager
管理需要跨帧的资源，比如渲染资源，可以gpu还在使用或者渲染帧还在使用
必须在渲染逻辑同步点进行增删改查的，使用Manager

