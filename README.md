# Path Tracing em C++

Projeto realizado por **Gabriel Madeira Martins** no âmbito do **Projeto Final de Engenharia Informática** na **Universidade da Beira Interior (UBI)**.

Este projeto implementa um ray tracer/path tracer em C++ com OpenGL compute shaders. O objetivo é demonstrar, de forma prática, conceitos de renderização fisicamente inspirada, estruturas de aceleração, materiais, iluminação global, acumulação temporal e interface de controlo em tempo real.

## Funcionalidades Principais

- Ray tracing em GPU através de compute shaders OpenGL.
- Interseção com esferas analíticas e malhas triangulares.
- BVH para acelerar interseções com malhas.
- Materiais difusos, especulares, vidro/refração e emissão.
- Texturas PBR com albedo, normal, roughness, metallic, ambient occlusion e height/parallax mapping.
- Cenas predefinidas com seletor de cena.
- Inspector ImGui para alterar materiais, transformações, parâmetros PBR, skybox e opções de renderização.
- Visualização de métricas de performance e mapas de complexidade da BVH/interseções.
- Pós-processamento com tone mapping, bloom e denoise simples.

## Como Clonar o Repositório

O modelo `app/resources/models/erato/erato.obj` excede o limite de 100 MB do GitHub e é distribuído através de [Git LFS](https://git-lfs.com/). Sem Git LFS, o clone pode concluir mas esse ficheiro fica incompleto e a cena correspondente não carrega corretamente.

1. Instalar [Git](https://git-scm.com/) e [Git LFS](https://git-lfs.com/).
2. Inicializar o Git LFS no PC (só é necessário uma vez):

```powershell
git lfs install
```

3. Clonar o repositório com submódulos:

```powershell
git clone --recurse-submodules https://github.com/GabrielMartinezzz/Path-Tracing.git
cd Path-Tracing
```

Se o repositório já tiver sido clonado sem LFS, correr na pasta do projeto:

```powershell
git lfs install
git lfs pull
git submodule update --init --recursive
```

## Como Dar Build

### Requisitos

- Windows 10/11.
- Visual Studio com workload de C++.
- CMake.
- Git, Git LFS e submódulos Git inicializados.
- Placa gráfica/driver com suporte a OpenGL 4.6.

### Passos no Windows com Visual Studio

1. Abrir a pasta raiz do projeto no Visual Studio como projeto CMake.
2. Confirmar que os submódulos estão inicializados:

```powershell
git submodule update --init --recursive
```

3. Selecionar uma configuração x64, de preferência `x64-Release`.
4. Compilar o alvo `raytracing.exe`.
5. Executar `raytracing.exe`.

### Build por Linha de Comandos

```powershell
cmake -S . -B out/build/x64-Release -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release --config Release
```
O executável gerado chama-se `raytracing.exe` que se encontrará na diretoria: \Path-Tracing\out\build\x64-Release\Release\raytracing.exe

Ao executar `raytracing.exe`, o utilizador pode reorganizar o layout das janelas ImGui; o viewport do path tracing abre com um tamanho confortável por defeito.

## Estrutura do Projeto

- `app/`: código da aplicação, janelas ImGui, presets de cena e recursos usados pela aplicação.
- `app/include/scenes/Scene1.hpp`: define as cenas, câmaras, luzes, esferas, modelos e texturas PBR.
- `app/include/GUI/`: componentes de interface para câmara, BVH, performance, skybox, inspector e seletor de cenas.
- `app/src/application.cpp`: ponto de entrada da aplicação, ciclo principal, troca de cenas e ligação entre GUI, câmara e renderer.
- `core/`: biblioteca estática com o renderer, parser de malhas, câmara, utilitários OpenGL e shaders.
- `core/resources/shaders/ComputeRayTracing.comp`: shader principal de ray tracing/path tracing.
- `core/resources/shaders/ComputePostProcessing.comp`: shader de pós-processamento.
- `core/src/ObjParser/ObjParser.cpp`: carregamento de modelos via Assimp e construção da BVH.
- `core/src/Renderer.cpp`: criação de buffers/texturas OpenGL e dispatch dos compute shaders.
- `thirdparty/`: dependências externas usadas pelo projeto.

## Documentação Doxygen

O projeto inclui um `Doxyfile` configurado para gerar documentação a partir de `README.md`, `app/` e `core/`.

Para gerar a documentação, instalar o Doxygen e executar na raiz do projeto:

```powershell
doxygen Doxyfile
```

Por defeito, a documentação HTML é gerada em `html/index.html`. 


## Notas Técnicas

- A BVH reduz o número de triângulos testados por raio, melhorando o desempenho em cenas com malhas complexas.
- O renderer acumula amostras ao longo de múltiplos frames. Sempre que a câmara ou a cena muda, a acumulação deve ser reiniciada para evitar ghosting.
- O pipeline PBR atual usa um conjunto de texturas por `ModelInstance` ou por esfera PBR. Malhas com vários materiais internos exigiriam guardar um índice de material por triângulo/submesh e enviar um buffer adicional de materiais para o shader.
- O parallax mapping desloca coordenadas UV com base no height map. Ele cria profundidade aparente, mas não altera a geometria real nem a silhueta dos objetos.
