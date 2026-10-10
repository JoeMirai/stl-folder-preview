#include <future>
#include <cmath>
#include <limits>

#include "loader.h"
#include "vertex.h"

Loader::Loader(QObject* parent, const QString& filename, bool is_reload) : QThread(parent), filename(filename), is_reload(is_reload)
{
    // Nothing to do here
}

void Loader::run()
{
    Mesh* mesh = load_stl();
    if (isInterruptionRequested()) { delete mesh; return; }
    if (mesh) {
        if (mesh->empty()) {
            emit error_empty_mesh();
            delete mesh;
        } else {
            emit got_mesh(mesh, is_reload);
            emit loaded_file(filename);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void parallel_sort(Vertex* begin, Vertex* end, int threads)
{
    if (threads < 2 || end - begin < 2) {
        std::sort(begin, end);
    } else {
        const auto mid = begin + (end - begin) / 2;
        if (threads == 2) {
            auto future = std::async(parallel_sort, begin, mid, threads / 2);
            std::sort(mid, end);
            future.wait();
        } else {
            auto a = std::async(std::launch::async, parallel_sort, begin, mid, threads / 2);
            auto b = std::async(std::launch::async, parallel_sort, mid, end, threads / 2);
            a.wait();
            b.wait();
        }
        std::inplace_merge(begin, mid, end);
    }
}

Mesh* mesh_from_verts(uint32_t tri_count, QVector<Vertex>& verts)
{
    // Save indicies as the second element in the array
    // (so that we can reconstruct triangle order after sorting)
    for (size_t i = 0; i < tri_count * 3; ++i) {
        verts[i].i = i;
    }

    // Check how many threads the hardware can safely support. This may return
    // 0 if the property can't be read so we shoud check for that too.
    const unsigned threads = 1; // One bounded worker, including vertex deduplication.

    // Sort the set of vertices (to deduplicate)
    if (QThread::currentThread()->isInterruptionRequested()) return nullptr;
    parallel_sort(verts.begin(), verts.end(), threads);
    if (QThread::currentThread()->isInterruptionRequested()) return nullptr;

    // This vector will store triangles as sets of 3 indices
    std::vector<GLuint> indices(tri_count * 3);

    // Go through the sorted vertex list, deduplicating and creating
    // an indexed geometry representation for the triangles.
    // Unique vertices are moved so that they occupy the first vertex_count
    // positions in the verts array.
    size_t vertex_count = 0;
    for (auto v : verts) {
        if (!vertex_count || v != verts[vertex_count - 1]) {
            verts[vertex_count++] = v;
        }
        indices[v.i] = vertex_count - 1;
    }
    verts.resize(vertex_count);

    std::vector<GLfloat> flat_verts;
    flat_verts.reserve(vertex_count * 3);
    for (auto v : verts) {
        flat_verts.push_back(v.x);
        flat_verts.push_back(v.y);
        flat_verts.push_back(v.z);
    }

    return new Mesh(std::move(flat_verts), std::move(indices));
}

////////////////////////////////////////////////////////////////////////////////

Mesh* Loader::load_stl()
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error_missing_file();
        return NULL;
    }

    if (isInterruptionRequested()) return nullptr;
    if (file.size() < 5) { emit error_bad_stl(); return nullptr; }

    // First, try to read the stl as an ASCII file
    if (file.read(5) == "solid") {
        file.readLine(); // skip solid name
        const auto line = file.readLine().trimmed();
        if (line.startsWith("facet") || line.startsWith("endsolid")) {
            file.seek(0);
            return read_stl_ascii(file);
        }
        // Otherwise, this STL is a binary stl but contains 'solid' as
        // the first five characters.  This is a bad life choice, but
        // we can gracefully handle it by falling through to the binary
        // STL reader below.
    }

    file.seek(0);
    return read_stl_binary(file);
}

Mesh* Loader::read_stl_binary(QFile& file)
{
    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    data.setFloatingPointPrecision(QDataStream::SinglePrecision);

    if (file.size() < 84) { emit error_bad_stl(); return nullptr; }
    file.seek(80);
    uint32_t tri_count = 0;
    data >> tri_count;
    const quint64 bytes = quint64(tri_count) * 50;
    if (quint64(file.size()) != 84 + bytes || tri_count > uint32_t(std::numeric_limits<int>::max()/3)) {
        emit error_bad_stl(); return nullptr;
    }
    if (!tri_count) return new Mesh({}, {});
    QVector<Vertex> verts(int(tri_count * 3));
    QByteArray buffer = file.read(qint64(bytes));
    if (quint64(buffer.size()) != bytes) { emit error_bad_stl(); return nullptr; }

    // Store vertices in the array, processing one triangle at a time.
    auto b = reinterpret_cast<const uchar*>(buffer.constData()) + 3 * sizeof(float);
    for (auto v = verts.begin(); v != verts.end(); v += 3) {
        if (isInterruptionRequested()) return nullptr;
        // Load vertex data from .stl file into vertices
        for (unsigned i = 0; i < 3; ++i) {
            qFromLittleEndian<float>(b, 3, &v[i]);
            if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y) || !std::isfinite(v[i].z)) { emit error_bad_stl(); return nullptr; }
            b += 3 * sizeof(float);
        }

        // Skip face attribute and next face's normal vector
        b += 3 * sizeof(float) + sizeof(uint16_t);
    }

    return mesh_from_verts(tri_count, verts);
}

Mesh* Loader::read_stl_ascii(QFile& file)
{
    file.readLine();
    QVector<Vertex> verts;
    bool ended = false;
    while (!file.atEnd()) {
        if (isInterruptionRequested()) return nullptr;
        const auto line = file.readLine().simplified();
        if (line.isEmpty()) continue;
        if (line.startsWith("endsolid")) { ended = true; break; }
        if (!line.startsWith("facet normal") || file.readLine().simplified() != "outer loop") {
            emit error_bad_stl(); return nullptr;
        }
        for (int i = 0; i < 3; ++i) {
            const auto fields = file.readLine().simplified().split(' ');
            if (fields.size() != 4 || fields[0] != "vertex") { emit error_bad_stl(); return nullptr; }
            bool a,b,c;
            float x = fields[1].toFloat(&a), y = fields[2].toFloat(&b), z = fields[3].toFloat(&c);
            if (!a || !b || !c || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                emit error_bad_stl(); return nullptr;
            }
            verts.push_back(Vertex(x,y,z));
        }
        if (file.readLine().trimmed() != "endloop" || file.readLine().trimmed() != "endfacet") {
            emit error_bad_stl(); return nullptr;
        }
    }
    if (!ended) { emit error_bad_stl(); return nullptr; }
    return mesh_from_verts(uint32_t(verts.size()/3), verts);
}
