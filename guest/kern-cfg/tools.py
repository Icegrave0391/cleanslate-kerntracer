###########################################
# Graph
###########################################
from kfunc_filter import should_filter_function
import re
import networkx as nx
from tqdm import tqdm

def gen_subgraph(static_graph, sys_entry_function=None, function_set=None, hops=2):
    """
    if sys_entry_function is set, and function_set is None: 
        Generate a subgraph starting from the sys_entry_function, including all reachable nodes.
    
    if sys_entry_function is None, and function_set is set:
        Generate a subgraph containing only the nodes in function_set.
    
    if both sys_entry_function and function_set are set:
        This is a "tricky" indicator.
        Generate a subgraph containing the nodes in function_set, and also bfs traverse the nodes in N `hops`
        from each node in function_set.
    """
    if not function_set:
        # If function_set is empty, use all reachable nodes from the sys_entry_function
        subgraph_nodes = nx.descendants(static_graph, sys_entry_function)
        subgraph_nodes.add(sys_entry_function)  # Include the entry function itself
    else:
        # Filter nodes to include only those in the function_set
        function_set = [f for f in function_set if not should_filter_function(f)]
        subgraph_nodes = set(function_set)

    if sys_entry_function is not None and function_set is not None:
        # Include nodes in function_set and traverse `hops` from each node
        if hops >= 0:
            for node in function_set:
                visited = set()
                queue = [(node, 0)]  # (current_node, current_hop)
                while queue:
                    current_node, current_hop = queue.pop(0)
                    if current_hop < hops and current_node not in visited:
                        visited.add(current_node)
                        neighbors = list(static_graph.successors(current_node))
                        subgraph_nodes.update(neighbors)
                        queue.extend([(neighbor, current_hop + 1) for neighbor in neighbors])
        
        else:   # hops = -1: consider all descendant nodes of function_set
            for node in function_set:
                _desc = nx.descendants(static_graph, node)
                subgraph_nodes.update(_desc)
                
        # Mark all nodes in the original function_set as "profiled"
        for node in function_set:
            if node in static_graph.nodes:
                static_graph.nodes[node]["profiled"] = True

    # Create the subgraph with the filtered nodes
    subgraph = static_graph.subgraph(subgraph_nodes).copy()
    return subgraph

def outedges_callgraph(graph, function_name, do_filter=True):
    """ 
    function_name is the source node (u), it will return all out-edges (u, v1), (u, v1),... of this node.
    """
    if function_name not in graph:
        print(f"Function {function_name} does not exist in the graph.")
        return []

    out_edges = []
    for _, target, edge_data in graph.out_edges(function_name, data=True):
        if do_filter and should_filter_function(target):
            continue
        out_edges.append((function_name, target, edge_data))
    
    return out_edges

def graph_to_markdown_tree(graph, note_profiled=False):
    """
    Convert a directed graph to a markdown tree representation
    """
    def dfs(node, indent=0, visited=set()):
        lines = []
        prefix = "  " * indent + "- " + node
        if note_profiled and graph.nodes[node].get("profiled", False):
            prefix += " <profiled>"
        lines.append(prefix)
        visited.add(node)
        for _, neighbor in graph.out_edges(node):
            if neighbor not in visited:
                lines.extend(dfs(neighbor, indent + 1, visited))
        return lines
    roots = [n for n in graph.nodes if graph.in_degree(n) == 0]
    all_lines = []
    for root in roots:
        all_lines.extend(dfs(root, indent=0, visited=set()))
    return "\n".join(all_lines)