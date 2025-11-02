#  Tarea 2 — Simulación Concurrente (Sistemas Operativos)

---

##  Autor
**Nombre:** Felipe Cuevas y Maximiliano Palma 
**Carrera:** Ingeniería Civil en Informática y Telecomunicaciones  
**Curso:** Sistemas Operativos  
**Tarea:** N°2 — Simulación concurrente con hilos  

---

##  Descripción general

Este programa implementa una **simulación concurrente** en la que uno o más **héroes** se mueven por un **mapa (grid)** siguiendo rutas predefinidas, enfrentándose a **monstruos** que reaccionan según su rango de visión y ataque.

Cada héroe y monstruo se ejecuta como un **hilo (thread)** independiente, lo que permite que los movimientos y ataques ocurran de forma simultánea.  
El objetivo de la simulación es que los héroes lleguen a su destino o eliminen a los enemigos encontrados.

---

##  Características principales

- Uso de **pthread** para ejecución concurrente.  
- Lectura de parámetros desde un archivo externo (`config.txt`).  
- Soporte para **múltiples héroes y monstruos**.  
- Ataques y movimientos controlados por condiciones de **visión y alcance**.  
- Impresión de un **resumen final** con el estado de cada entidad.  

---

## Archivos del proyecto

```
tarea2so/
├── main.cpp        # Código principal de la simulación
├── config.txt      # Archivo de configuración
└── README.md       # Documento de descripción (este archivo)
```

---

##  Configuración (`config.txt`)

Ejemplo:

```
GRID_SIZE 30 14
HERO_COUNT 2

HERO_1_HP 120
HERO_1_ATTACK_DAMAGE 20
HERO_1_ATTACK_RANGE 1
HERO_1_START 2 2
HERO_1_PATH (4,2) (8,2) (12,4) (16,6) (20,6)

HERO_2_HP 100
HERO_2_ATTACK_DAMAGE 25
HERO_2_ATTACK_RANGE 1
HERO_2_START 5 12
HERO_2_PATH (6,11) (8,10) (10,9) (12,8) (15,7)

MONSTER_COUNT 4
MONSTER_1_HP 50
MONSTER_1_ATTACK_DAMAGE 8
MONSTER_1_VISION_RANGE 6
MONSTER_1_ATTACK_RANGE 1
MONSTER_1_COORDS 8 2

MONSTER_2_HP 60
MONSTER_2_ATTACK_DAMAGE 10
MONSTER_2_VISION_RANGE 5
MONSTER_2_ATTACK_RANGE 1
MONSTER_2_COORDS 12 6

MONSTER_3_HP 80
MONSTER_3_ATTACK_DAMAGE 12
MONSTER_3_VISION_RANGE 5
MONSTER_3_ATTACK_RANGE 2
MONSTER_3_COORDS 20 6

MONSTER_4_HP 70
MONSTER_4_ATTACK_DAMAGE 9
MONSTER_4_VISION_RANGE 4
MONSTER_4_ATTACK_RANGE 1
MONSTER_4_COORDS 24 10

```

---

##  Funcionamiento

1. Se leen los parámetros desde `config.txt`.  
2. Se crean los hilos correspondientes al héroe y los monstruos.  
3. El héroe se mueve según su ruta definida.  
4. Los monstruos se activan cuando detectan al héroe dentro de su rango de visión.  
5. Si el héroe entra en el rango de ataque de un monstruo, ambos combaten hasta que uno muere.  
6. Al finalizar la simulación, se imprime un **resumen final** con el estado de todos los participantes.

---

##  Compilación y ejecución

###  Compilar:
```bash
g++ -std=c++17 -pthread main.cpp -o doom_sim
```

###  Ejecutar:
```bash
./doom_sim config.txt
```

---

##  Ejemplo de salida

```
=== DOOM-SIM (FIN) ===
Resumen final:
 H1 -> VIVO/META HP=165 Pos=(16,2)
 H2 -> MUERTO HP=-3 Pos=(12,10)
 M1 -> MUERTO HP=-20 Pos=(8,2)
 M2 -> VIVO HP=15 Pos=(12,8)
 M3 -> MUERTO HP=-5 Pos=(6,16)
 M4 -> MUERTO HP=-5 Pos=(6,16)
 M5 -> VIVO HP=100 Pos=(35,15)
[HERO 2] Murió durante combate.
[HERO 2] Sale de combate.
Simulación finalizada.
```

---



---

## Conclusión

- El programa se ejecuta correctamente con distintas configuraciones en `config.txt`.  
- Se manejan los casos de múltiples héroes y varios monstruos sin errores de concurrencia.  
- Se sincronizan correctamente los hilos durante el movimiento y combate.
