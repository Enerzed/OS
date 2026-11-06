#pragma once
#include <vector>
#include <queue>
#include <unordered_set>
#include <set>

class PageManager {
public:
    explicit PageManager(int num_frames) : capacity(num_frames), frames(num_frames, -1) {}

    void accessPage(int page_id) {
        last_fault = false;
        last_hit = false;
        last_victim_page = -1;
        last_victim_frame = -1;
        last_loaded_page = page_id;
        last_loaded_from_disk = (disk_pages.count(page_id) > 0);

        // Hit — page already in RAM
        if (present_pages.count(page_id)) {
            last_hit = true;
            return;
        }

        last_fault = true;

        int target_frame = -1;
        if (present_pages.size() < static_cast<size_t>(capacity)) {
            // Free frame available
            for (int i = 0; i < capacity; ++i) {
                if (frames[i] == -1) { target_frame = i; break; }
            }
        } else {
            // FIFO eviction: victim = head of the queue
            int victim = fifo_queue.front();
            fifo_queue.pop();
            present_pages.erase(victim);
            disk_pages.insert(victim);
            last_victim_page = victim;

            for (int i = 0; i < capacity; ++i) {
                if (frames[i] == victim) {
                    target_frame = i;
                    last_victim_frame = i;
                    break;
                }
            }
        }

        frames[target_frame] = page_id;
        fifo_queue.push(page_id);
        present_pages.insert(page_id);
        disk_pages.erase(page_id); // if it was on disk, it's back in RAM now
    }

    // Accessors used by the visualization
    const std::vector<int>& getFrames()   const { return frames; }
    const std::queue<int>&  getQueue()    const { return fifo_queue; }
    const std::set<int>&    getDiskPages() const { return disk_pages; }
    int getCapacity()        const { return capacity; }

    bool wasLastFault()        const { return last_fault; }
    bool wasLastHit()          const { return last_hit; }
    int  getLastVictimPage()   const { return last_victim_page; }
    int  getLastVictimFrame()  const { return last_victim_frame; }
    int  getLastLoadedPage()   const { return last_loaded_page; }
    bool wasLastLoadedFromDisk() const { return last_loaded_from_disk; }

private:
    int capacity;
    std::vector<int> frames;
    std::queue<int> fifo_queue;
    std::unordered_set<int> present_pages;
    std::set<int> disk_pages;

    bool last_fault = false;
    bool last_hit   = false;
    int  last_victim_page  = -1;
    int  last_victim_frame = -1;
    int  last_loaded_page  = -1;
    bool last_loaded_from_disk = false;
};