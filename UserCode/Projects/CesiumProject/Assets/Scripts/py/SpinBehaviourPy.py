from mqengine import ScriptBehaviour, RotationComponent


class SpinBehaviourPy(ScriptBehaviour):
    def __init__(self):
        super().__init__()
        self.speed = 60.0

    def on_start(self):
        print(f"[SpinBehaviourPy] 实体 {self.entity.id} Python 脚本已启动")

    def on_update(self, dt: float):
        rot = self.get_component(RotationComponent)
        if rot is not None:
            rot.rotation.y += self.speed * dt
