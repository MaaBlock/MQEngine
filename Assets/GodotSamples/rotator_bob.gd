extends Node3D
class_name GodotRotatorBob

@export var spin_speed: float = 90.0
@export var bob_amplitude: float = 0.35
@export var bob_frequency: float = 2.5

var elapsed_time: float = 0.0
var base_y: float = 0.0

func _ready() -> void:
    base_y = position.y
    print("[GodotRotatorBob] GDScript -> Python 脚本已启动, entity=", entity.id)

func _process(delta: float) -> void:
    elapsed_time += delta
    rotation_degrees.y += spin_speed * delta
    position.y = base_y + sin(elapsed_time * bob_frequency) * bob_amplitude
