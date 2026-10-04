import random
import os

def generar_lineal(n, filename):
    """Genera una cadena donde cada actividad depende estrictamente de la anterior."""
    with open(filename, 'w') as f:
        for i in range(1, n + 1):
            deps = str(i - 1) if i > 1 else ""
            f.write(f"{i}: Tarea_Secuencial_{i}: {random.randint(100, 5000)}: {deps}\n")
    print(f"Generado {filename} con {n} actividades (Lineal).")

def generar_ancho(n, filename):
    """Genera actividades totalmente independientes (sin dependencias)."""
    with open(filename, 'w') as f:
        for i in range(1, n + 1):
            f.write(f"{i}: Tarea_Independiente_{i}: {random.randint(100, 5000)}:\n")
    print(f"Generado {filename} con {n} actividades (Ancho).")

def generar_aleatorio(n, filename):
    """Genera un DAG aleatorio. Las dependencias siempre apuntan a IDs menores para evitar ciclos."""
    with open(filename, 'w') as f:
        for i in range(1, n + 1):
            # 20% de probabilidad de no poner duración para probar la asignación aleatoria del parser
            dur = "" if random.random() < 0.2 else str(random.randint(100, 5000))
            
            deps = []
            if i > 1 and random.random() < 0.6: # 60% de probabilidad de tener dependencias
                num_deps = random.randint(1, min(4, i - 1))
                deps = random.sample(range(1, i), num_deps)
            
            deps_str = ",".join(map(str, deps))
            f.write(f"{i}: Tarea_Aleatoria_{i}: {dur}: {deps_str}\n")
    print(f"Generado {filename} con {n} actividades (Aleatorio).")

def generar_falla(filename):
    """Genera un plan pequeño donde una actividad está diseñada para fallar."""
    with open(filename, 'w') as f:
        f.write("1: prender_carbon: 500:\n")
        f.write("2: comprar_carne: 1200:\n")
        f.write("3: error_corte_luz: 300: 1\n")
        f.write("4: asar_longaniza: 800: 2,3\n")
        f.write("5: servir_mesa: 100: 4\n")
    print(f"Generado {filename} con 5 actividades (Rama con falla).")

if __name__ == "__main__":
    # Crear carpeta para guardar los planes si no existe
    if not os.path.exists("tests"):
        os.makedirs("tests")
        
    generar_lineal(10000, "tests/plan_estres_lineal.txt")
    generar_ancho(10000, "tests/plan_estres_ancho.txt")
    generar_aleatorio(10000, "tests/plan_estres_aleatorio.txt")
    generar_falla("tests/plan_falla.txt")
    
    print("\n¡Archivos de prueba generados exitosamente en la carpeta 'tests/'!")