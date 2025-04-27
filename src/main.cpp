#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>

// Global variables for synchronization
constexpr int NUM_JOGADORES = 4;
std::counting_semaphore<NUM_JOGADORES>* cadeira_sem = new std::counting_semaphore<NUM_JOGADORES>(NUM_JOGADORES - 1);
std::condition_variable music_cv;
std::mutex music_mutex;
std::atomic<bool> musica_parada{false};
std::atomic<bool> jogo_ativo{true};
std::mutex cout_mutex;
std::vector<int> jogadores_ativos;
std::mutex jogadores_mutex;
std::vector<std::pair<int, int>> cadeiras_ocupadas;
std::mutex cadeira_mutex;

void sleep_random() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
}
/*
 * Uso básico de um counting_semaphore em C++:
 * 
 * O `std::counting_semaphore` é um mecanismo de sincronização que permite controlar o acesso a um recurso compartilhado 
 * com um número máximo de acessos simultâneos. Neste projeto, ele é usado para gerenciar o número de cadeiras disponíveis.
 * Inicializamos o semáforo com `n - 1` para representar as cadeiras disponíveis no início do jogo. 
 * Cada jogador que tenta se sentar precisa fazer um `acquire()`, e o semáforo permite que até `n - 1` jogadores 
 * ocupem as cadeiras. Quando todos os assentos estão ocupados, jogadores adicionais ficam bloqueados até que 
 * o coordenador libere o semáforo com `release()`, sinalizando a eliminação dos jogadores.
 * O método `release()` também pode ser usado para liberar múltiplas permissões de uma só vez, por exemplo: `cadeira_sem.release(3);`,
 * o que permite destravar várias threads de uma só vez, como é feito na função `liberar_threads_eliminadas()`.
 *
 * Métodos da classe `std::counting_semaphore`:
 * 
 * 1. `acquire()`: Decrementa o contador do semáforo. Bloqueia a thread se o valor for zero.
 *    - Exemplo de uso: `cadeira_sem.acquire();` // Jogador tenta ocupar uma cadeira.
 * 
 * 2. `release(int n = 1)`: Incrementa o contador do semáforo em `n`. Pode liberar múltiplas permissões.
 *    - Exemplo de uso: `cadeira_sem.release(2);` // Libera 2 permissões simultaneamente.
 */
class JogoDasCadeiras {
public:
    JogoDasCadeiras(int num_jogadores)
        : num_jogadores(num_jogadores), cadeiras(num_jogadores - 1) {
        for (int i = 1; i <= num_jogadores; ++i) {
            jogadores_ativos.push_back(i);
        }
    }

    void iniciar_rodada() {
        {
            std::lock_guard<std::mutex> lock(jogadores_mutex);
            cadeiras = jogadores_ativos.size() - 1;
        }

        {
            std::lock_guard<std::mutex> lock(cadeira_mutex);
            cadeiras_ocupadas.clear();
        }

        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n-----------------------------------------------\n";
            std::cout << "Iniciando rodada com " << jogadores_ativos.size()
                      << " jogadores e " << cadeiras << " cadeiras.\n";
            std::cout << "A música está tocando... 🎵\n";
        }
    }

    void parar_musica() {
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n> A música parou! Os jogadores estão tentando se sentar...\n";
        }

        {
            std::lock_guard<std::mutex> lock(music_mutex);
            musica_parada = true;
        }
        music_cv.notify_all();
    }

    void eliminar_jogador(int jogador_id) {
        {
            std::lock_guard<std::mutex> lock(jogadores_mutex);
            jogadores_ativos.erase(std::remove(jogadores_ativos.begin(), jogadores_ativos.end(), jogador_id),
                                    jogadores_ativos.end());
        }
    }

    void exibir_resultado_rodada(int eliminado_id) {
        std::lock_guard<std::mutex> lock_out(cout_mutex);
        std::cout << "\n-----------------------------------------------\n";
        for (size_t i = 0; i < cadeiras_ocupadas.size(); ++i) {
            std::cout << "[Cadeira " << i + 1 << "]: Ocupada por P" << cadeiras_ocupadas[i].first << "\n";
        }
        std::cout << "\nJogador P" << eliminado_id << " não conseguiu uma cadeira e foi eliminado!\n";
        std::cout << "-----------------------------------------------\n";
    }

    int get_num_jogadores() const { return num_jogadores; }
    int get_cadeiras() const { return cadeiras; }
    const std::vector<int>& get_jogadores_ativos() const { return jogadores_ativos; }

private:
    int num_jogadores;
    int cadeiras;
};

class Jogador {
public:
    Jogador(int id, JogoDasCadeiras& jogo)
        : id(id), jogo(jogo), eliminado(false) {}

    void tentar_ocupar_cadeira() {
        if (cadeira_sem->try_acquire()) {
            std::lock_guard<std::mutex> lock(cadeira_mutex);
            cadeiras_ocupadas.emplace_back(id, id);
        } else {
            eliminado = true;
        }
    }

    void verificar_eliminacao() {
        if (eliminado) {
            jogo.eliminar_jogador(id);
        }
    }

    void joga() {
        while (jogo_ativo) {
            {
                std::unique_lock<std::mutex> lock(music_mutex);
                music_cv.wait(lock, []{ return musica_parada.load(); });
            }

            if (!jogo_ativo) break;

            tentar_ocupar_cadeira();
            verificar_eliminacao();

            {
                std::unique_lock<std::mutex> lock(music_mutex);
                music_cv.wait(lock, []{ return !musica_parada.load(); });
            }
        }
    }

private:
    int id;
    JogoDasCadeiras& jogo;
    bool eliminado;
};

class Coordenador {
public:
    Coordenador(JogoDasCadeiras& jogo)
        : jogo(jogo) {}

    void iniciar_jogo() {
        while (jogo.get_jogadores_ativos().size() > 1) {
            jogo.iniciar_rodada();
            sleep_random();
            jogo.parar_musica();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            liberar_threads_eliminadas();

            {
                std::lock_guard<std::mutex> lock(music_mutex);
                musica_parada = false;
            }

            delete cadeira_sem;
            cadeira_sem = new std::counting_semaphore<NUM_JOGADORES>(jogo.get_jogadores_ativos().size() - 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        jogo_ativo = false;
        music_cv.notify_all();

        if (!jogo.get_jogadores_ativos().empty()) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n-----------------------------------------------\n";
            std::cout << "🏆 Vencedor: Jogador P" << jogo.get_jogadores_ativos()[0] << "! Parabéns! 🏆\n";
            std::cout << "-----------------------------------------------\n";
            std::cout << "\nObrigado por jogar o Jogo das Cadeiras Concorrente!\n";
        }

        delete cadeira_sem;
    }

    void liberar_threads_eliminadas() {
        std::vector<int> jogadores_sentados;
        {
            std::lock_guard<std::mutex> lock(cadeira_mutex);
            for (auto& [id, _] : cadeiras_ocupadas) {
                jogadores_sentados.push_back(id);
            }
        }

        int eliminado_id = -1;
        for (int id : jogo.get_jogadores_ativos()) {
            if (std::find(jogadores_sentados.begin(), jogadores_sentados.end(), id) == jogadores_sentados.end()) {
                eliminado_id = id;
                jogo.eliminar_jogador(id);
                break;
            }
        }

        jogo.exibir_resultado_rodada(eliminado_id);
        cadeira_sem->release(NUM_JOGADORES);
    }

private:
    JogoDasCadeiras& jogo;
};

int main() {
    std::cout << "-----------------------------------------------\n";
    std::cout << "Bem-vindo ao Jogo das Cadeiras Concorrente!\n";
    std::cout << "-----------------------------------------------\n\n";

    JogoDasCadeiras jogo(NUM_JOGADORES);
    Coordenador coordenador(jogo);
    std::vector<std::thread> jogadores_threads;

    std::vector<Jogador> jogadores_objs;
    for (int i = 1; i <= NUM_JOGADORES; ++i) {
        jogadores_objs.emplace_back(i, jogo);
    }

    for (int i = 0; i < NUM_JOGADORES; ++i) {
        jogadores_threads.emplace_back(&Jogador::joga, &jogadores_objs[i]);
    }

    std::thread coordenador_thread(&Coordenador::iniciar_jogo, &coordenador);

    for (auto& t : jogadores_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    if (coordenador_thread.joinable()) {
        coordenador_thread.join();
    }

    return 0;
}